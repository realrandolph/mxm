/*
 * AudioAlsa.cpp - device-class which implements ALSA-PCM-output
 *
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 *
 * This file is part of MXM (Musica ex Machina), a fork of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */


#include "AudioAlsa.h"

#ifdef MXM_HAVE_ALSA

#include <poll.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <thread>
#include <utility>
#include <vector>

#include "endian_handling.h"
#include "AudioBufferView.h"
#include "AudioEngine.h"
#include "ConfigManager.h"

namespace mxm
{

struct AudioAlsa::TrackCapture
{
	AudioEngine* engine = nullptr;
	quint64 trackId = 0;
	QString device;
	std::atomic_bool stop{false};
	std::thread reader;
};

namespace
{

constexpr auto CaptureChannels = ch_cnt_t{2};

auto isInputHint(const char* name, const char* ioid) -> bool
{
	if (name == nullptr || *name == '\0') { return false; }
	// Pure discard and USB playback stream hints are never capture endpoints.
	if (std::strcmp(name, "null") == 0 || std::strncmp(name, "usbstream:", 10) == 0)
	{
		return false;
	}
	if (ioid == nullptr || *ioid == '\0') { return true; }
	return std::strstr(ioid, "Input") != nullptr || std::strstr(ioid, "Both") != nullptr;
}

auto hintLabel(const char* name, const char* description) -> QString
{
	auto label = description != nullptr
		? QString::fromUtf8(description).simplified() : QString{};
	if (label.isEmpty()) { return QString::fromUtf8(name); }
	return label + QStringLiteral(" (") + QString::fromUtf8(name) + QLatin1Char(')');
}

struct CaptureStream
{
	snd_pcm_t* handle = nullptr;
	ch_cnt_t channels = 0;
	bool integer = false;
};

auto configureCapture(snd_pcm_t* handle, snd_pcm_format_t format, ch_cnt_t channels,
	sample_rate_t rate, f_cnt_t periodFrames) -> bool
{
	snd_pcm_hw_params_t* hwParams = nullptr;
	snd_pcm_hw_params_alloca(&hwParams);
	if (snd_pcm_hw_params_any(handle, hwParams) < 0) { return false; }
	if (snd_pcm_hw_params_set_access(handle, hwParams, SND_PCM_ACCESS_RW_INTERLEAVED) < 0)
	{
		return false;
	}
	if (snd_pcm_hw_params_set_format(handle, hwParams, format) < 0) { return false; }
	if (snd_pcm_hw_params_set_channels(handle, hwParams, channels) < 0) { return false; }

	if (snd_pcm_hw_params_set_rate(handle, hwParams, rate, 0) < 0)
	{
		auto nearRate = static_cast<unsigned int>(rate);
		int direction = 0;
		if (snd_pcm_hw_params_set_rate_near(handle, hwParams, &nearRate, &direction) < 0
			|| nearRate != static_cast<unsigned int>(rate))
		{
			return false;
		}
	}

	auto periodSize = static_cast<snd_pcm_uframes_t>(std::max<f_cnt_t>(periodFrames, 1));
	int direction = 0;
	snd_pcm_hw_params_set_period_size_near(handle, hwParams, &periodSize, &direction);
	auto bufferSize = periodSize * 4;
	snd_pcm_hw_params_set_buffer_size_near(handle, hwParams, &bufferSize);

	return snd_pcm_hw_params(handle, hwParams) >= 0;
}

auto openCaptureStream(const QString& device, sample_rate_t rate, f_cnt_t periodFrames) -> CaptureStream
{
	CaptureStream stream;
	const auto name = device.toLatin1();
	if (snd_pcm_open(&stream.handle, name.constData(), SND_PCM_STREAM_CAPTURE,
		SND_PCM_NONBLOCK) < 0)
	{
		stream.handle = nullptr;
		return stream;
	}

	const auto formats = std::array<std::pair<snd_pcm_format_t, bool>, 2>{{
		{SND_PCM_FORMAT_FLOAT, false},
		{SND_PCM_FORMAT_S16, true},
	}};
	for (const auto& [format, integer] : formats)
	{
		for (const auto channels : {CaptureChannels, ch_cnt_t{1}})
		{
			if (!configureCapture(stream.handle, format, channels, rate, periodFrames))
			{
				continue;
			}
			stream.channels = channels;
			stream.integer = integer;
			snd_pcm_nonblock(stream.handle, 1);
			return stream;
		}
	}

	snd_pcm_close(stream.handle);
	stream.handle = nullptr;
	return stream;
}

} // namespace

AudioAlsa::AudioAlsa( bool & _success_ful, AudioEngine*  _audioEngine ) :
	AudioDevice(std::clamp<ch_cnt_t>(
		ConfigManager::inst()->value("audioalsa", "channels").toInt(),
		DEFAULT_CHANNELS,
		DEFAULT_CHANNELS), _audioEngine),
	m_handle( nullptr ),
	m_hwParams( nullptr ),
	m_swParams(nullptr)
{
	_success_ful = false;

	if( setenv( "PULSE_ALSA_HOOK_CONF", "/dev/null", 0 ) )
	{
		fprintf( stderr,
		"Could not avoid possible interception by PulseAudio\n" );
	}

	if (int err = snd_pcm_open(&m_handle, probeDevice().toLatin1().constData(), SND_PCM_STREAM_PLAYBACK, 0); err < 0)
	{
		printf( "Playback open error: %s\n", snd_strerror( err ) );
		return;
	}

	snd_pcm_hw_params_malloc( &m_hwParams );
	snd_pcm_sw_params_malloc( &m_swParams );

	if (int err = setHWParams(channels(), SND_PCM_ACCESS_RW_INTERLEAVED); err < 0)
	{
		printf( "Setting of hwparams failed: %s\n",
							snd_strerror( err ) );
		return;
	}
	if (int err = setSWParams(); err < 0)
	{
		printf( "Setting of swparams failed: %s\n",
							snd_strerror( err ) );
		return;
	}

	// set FD_CLOEXEC flag for all file descriptors so forked processes
	// do not inherit them
	int count = snd_pcm_poll_descriptors_count( m_handle );
	auto ufds = new pollfd[count];
	snd_pcm_poll_descriptors( m_handle, ufds, count );
	for (int i = 0; i < std::max(3, count); ++i)
	{
		const int fd = ( i >= count ) ? ufds[0].fd+i : ufds[i].fd;
		int oldflags = fcntl( fd, F_GETFD, 0 );
		if( oldflags < 0 )
			continue;
		oldflags |= FD_CLOEXEC;
		fcntl( fd, F_SETFD, oldflags );
	}
	delete[] ufds;
	m_monitoredDevice = probeDevice();
	m_supportsCapture = !inputSources().empty();
	_success_ful = true;
}




AudioAlsa::~AudioAlsa()
{
	stopTrackCaptures();

	if( m_handle != nullptr )
	{
		snd_pcm_close( m_handle );
	}

	if( m_hwParams != nullptr )
	{
		snd_pcm_hw_params_free( m_hwParams );
	}

	if( m_swParams != nullptr )
	{
		snd_pcm_sw_params_free( m_swParams );
	}
}




QString AudioAlsa::probeDevice()
{
	QString dev = ConfigManager::inst()->value( "audioalsa", "device" );
	if( dev == "" )
	{
		if( getenv( "AUDIODEV" ) != nullptr )
		{
			return getenv( "AUDIODEV" );
		}
		return "default";
	}
	return dev;
}




AudioAlsa::DeviceInfoCollection AudioAlsa::getAvailableDevices()
{
	DeviceInfoCollection deviceInfos;

	char** hints = nullptr;

	/* Enumerate sound devices */
	int err = snd_device_name_hint(-1, "pcm", (void***)&hints);
	if (err != 0)
	{
		return deviceInfos;
	}

	char** n = hints;
	while (*n != nullptr)
	{
		char *name = snd_device_name_get_hint(*n, "NAME");
		char *description = snd_device_name_get_hint(*n, "DESC");

		if (name != 0 && description != 0)
		{
			deviceInfos.push_back(DeviceInfo(QString(name), QString(description)));
		}

		free(name);
		free(description);

		n++;
	}

	//Free the hint buffer
	snd_device_name_free_hint((void**)hints);

	return deviceInfos;
}




auto AudioAlsa::supportsTrackInputPorts() const -> bool
{
	return m_supportsCapture;
}




auto AudioAlsa::inputSources() const -> std::vector<AudioInputSource>
{
	auto sources = std::vector<AudioInputSource>{};

	char** hints = nullptr;
	if (snd_device_name_hint(-1, "pcm", reinterpret_cast<void***>(&hints)) != 0)
	{
		return sources;
	}

	for (auto** hint = hints; hint != nullptr && *hint != nullptr; ++hint)
	{
		auto* name = snd_device_name_get_hint(*hint, "NAME");
		auto* description = snd_device_name_get_hint(*hint, "DESC");
		auto* ioid = snd_device_name_get_hint(*hint, "IOID");

		if (isInputHint(name, ioid))
		{
			const auto deviceName = QString::fromUtf8(name);
			sources.push_back(AudioInputSource{
				QStringLiteral("alsa-pcm:") + deviceName,
				hintLabel(name, description),
				CaptureChannels,
				AudioChannelLayout::stereo(),
				{0, 1},
				true,
				{deviceName},
			});
		}

		free(name);
		free(description);
		free(ioid);
	}

	snd_device_name_free_hint(reinterpret_cast<void**>(hints));
	return sources;
}




auto AudioAlsa::registerTrackInputPort(quint64 id, const QString& name, ch_cnt_t channels) -> bool
{
	Q_UNUSED(name);
	if (!supportsTrackInputPorts() || id == 0 || channels != CaptureChannels)
	{
		return false;
	}

	unregisterTrackInputPort(id);
	auto capture = std::make_unique<TrackCapture>();
	capture->engine = audioEngine();
	capture->trackId = id;
	return m_trackCaptures.emplace(id, std::move(capture)).second;
}




void AudioAlsa::unregisterTrackInputPort(quint64 id)
{
	const auto found = m_trackCaptures.find(id);
	if (found == m_trackCaptures.end()) { return; }
	stopTrackCapture(*found->second);
	m_trackCaptures.erase(found);
}




void AudioAlsa::setTrackInputSource(quint64 id, const QStringList& endpointPorts)
{
	const auto found = m_trackCaptures.find(id);
	if (found == m_trackCaptures.end()) { return; }

	auto& capture = *found->second;
	stopTrackCapture(capture);
	capture.device = endpointPorts.isEmpty() ? QString{} : endpointPorts.front();
	if (!capture.device.isEmpty() && AudioDevice::isRunning())
	{
		startTrackCapture(capture);
	}
}




void AudioAlsa::startTrackCapture(TrackCapture& capture)
{
	if (capture.device.isEmpty() || capture.reader.joinable()) { return; }
	capture.stop.store(false, std::memory_order_release);
	capture.reader = std::thread(&AudioAlsa::captureThread, this, std::ref(capture));
}




void AudioAlsa::stopTrackCapture(TrackCapture& capture)
{
	capture.stop.store(true, std::memory_order_release);
	if (capture.reader.joinable()) { capture.reader.join(); }
}




void AudioAlsa::stopTrackCaptures()
{
	for (auto& [id, capture] : m_trackCaptures)
	{
		Q_UNUSED(id);
		capture->stop.store(true, std::memory_order_release);
	}
	for (auto& [id, capture] : m_trackCaptures)
	{
		Q_UNUSED(id);
		if (capture->reader.joinable()) { capture->reader.join(); }
	}
}




void AudioAlsa::captureThread(TrackCapture& capture)
{
	const auto framesPerRead = std::max<f_cnt_t>(audioEngine()->framesPerAudioBuffer(), 1);
	auto stream = openCaptureStream(capture.device, sampleRate(), framesPerRead);
	auto* pcm = stream.handle;
	if (pcm == nullptr)
	{
		qWarning("ALSA: cannot open capture device '%s'", qUtf8Printable(capture.device));
		return;
	}

	snd_pcm_start(pcm);

	std::vector<float> interleaved(static_cast<std::size_t>(framesPerRead) * stream.channels);
	std::vector<int16_t> integerFrames;
	if (stream.integer)
	{
		integerFrames.resize(interleaved.size());
	}

	auto descriptorCount = snd_pcm_poll_descriptors_count(pcm);
	if (descriptorCount <= 0) { descriptorCount = 1; }
	std::vector<pollfd> descriptors(static_cast<std::size_t>(descriptorCount));

	auto restart = [pcm]() -> bool
	{
		if (snd_pcm_prepare(pcm) < 0) { return false; }
		snd_pcm_start(pcm);
		return true;
	};

	while (!capture.stop.load(std::memory_order_acquire) && AudioDevice::isRunning())
	{
		const auto count = snd_pcm_poll_descriptors(pcm, descriptors.data(),
			static_cast<unsigned int>(descriptors.size()));
		if (count <= 0) { break; }

		const auto ready = poll(descriptors.data(), static_cast<nfds_t>(count), 50);
		if (ready < 0)
		{
			if (errno == EINTR) { continue; }
			break;
		}
		if (ready == 0) { continue; }

		void* destination = stream.integer
			? static_cast<void*>(integerFrames.data()) : static_cast<void*>(interleaved.data());
		const auto framesRead = snd_pcm_readi(pcm, destination, framesPerRead);
		if (framesRead == -EAGAIN) { continue; }

		if (framesRead < 0)
		{
			const auto error = static_cast<int>(framesRead);
			if (error == -ENODEV || error == -EBADFD
				|| snd_pcm_state(pcm) == SND_PCM_STATE_DISCONNECTED)
			{
				break;
			}
			if (error == -EINTR) { continue; }
			if (error == -EPIPE)
			{
				if (!restart()) { break; }
				continue;
			}
#ifdef ESTRPIPE
			if (error == -ESTRPIPE)
			{
				auto resumed = 0;
				while ((resumed = snd_pcm_resume(pcm)) == -EAGAIN)
				{
					if (capture.stop.load(std::memory_order_acquire)
						|| !AudioDevice::isRunning())
					{
						break;
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
				}
				if (resumed < 0 && !restart()) { break; }
				continue;
			}
#endif
			break;
		}

		if (framesRead == 0) { continue; }

		if (stream.integer)
		{
			const auto samples = static_cast<std::size_t>(framesRead) * stream.channels;
			for (std::size_t sample = 0; sample < samples; ++sample)
			{
				interleaved[sample] = static_cast<float>(integerFrames[sample]) / 32767.0f;
			}
		}

		capture.engine->pushTrackInputFrames(capture.trackId,
			InterleavedBufferView<const float>{interleaved.data(), stream.channels,
				static_cast<f_cnt_t>(framesRead)});
	}

	snd_pcm_close(pcm);
}




int AudioAlsa::handleError( int _err )
{
	if (_err == -ENODEV || _err == -EBADFD)
	{
		return _err;
	}
	if( _err == -EPIPE )
	{
		// under-run
		_err = snd_pcm_prepare( m_handle );
		if( _err < 0 )
			printf( "Can't recover from underrun, prepare "
					"failed: %s\n", snd_strerror( _err ) );
		return ( 0 );
	}
#ifdef ESTRPIPE
	else if( _err == -ESTRPIPE )
	{
		while( ( _err = snd_pcm_resume( m_handle ) ) == -EAGAIN )
		{
			sleep( 1 );	// wait until the suspend flag
					// is released
		}

		if( _err < 0 )
		{
			_err = snd_pcm_prepare( m_handle );
			if( _err < 0 )
				printf( "Can't recover from suspend, prepare "
					"failed: %s\n", snd_strerror( _err ) );
		}
		return ( 0 );
	}
#endif
	return _err;
}




void AudioAlsa::startProcessingImpl()
{
	start(QThread::HighPriority);
	for (auto& [id, capture] : m_trackCaptures)
	{
		Q_UNUSED(id);
		startTrackCapture(*capture);
	}
}




void AudioAlsa::stopProcessingImpl()
{
	stopTrackCaptures();
	stopProcessingThread( this );
}

void AudioAlsa::run()
{
	const auto framesPerAudioBuffer = audioEngine()->framesPerAudioBuffer();
	auto buf = std::vector<float>(framesPerAudioBuffer * channels());
	while (AudioDevice::isRunning())
	{
		if (m_deviceDisconnected.load(std::memory_order_relaxed))
		{
			msleep(100);
			continue;
		}

		audioEngine()->renderNextBuffer({buf.data(), channels(), framesPerAudioBuffer});

		if (const auto framesWritten = snd_pcm_writei(m_handle, buf.data(), framesPerAudioBuffer); framesWritten < 0)
		{
			const auto err = handleError(framesWritten);
			if (err == -ENODEV || err == -EBADFD || (m_handle && snd_pcm_state(m_handle) == SND_PCM_STATE_DISCONNECTED))
			{
				m_deviceDisconnected.store(true, std::memory_order_release);
				if (!m_disconnectionReported.exchange(true, std::memory_order_acq_rel))
				{
					reportDeviceChange(false, m_monitoredDevice, QString{}, false);
				}
			}
			continue;
		}
	}
}




int AudioAlsa::setHWParams( const ch_cnt_t _channels, snd_pcm_access_t _access )
{
	// choose all parameters
	if (int err = snd_pcm_hw_params_any(m_handle, m_hwParams); err < 0)
	{
		printf( "Broken configuration for playback: no configurations "
				"available: %s\n", snd_strerror( err ) );
		return err;
	}

	// set the interleaved read/write format
	if (int err = snd_pcm_hw_params_set_access(m_handle, m_hwParams, _access); err < 0)
	{
		printf( "Access type not available for playback: %s\n",
							snd_strerror( err ) );
		return err;
	}

	// set the sample format
	if (int err = snd_pcm_hw_params_set_format(m_handle, m_hwParams, SND_PCM_FORMAT_FLOAT); err < 0)
	{
		printf("Failed to set PCM format: %s", snd_strerror(err));
		return err;
	}

	// set the count of channels
	if (int err = snd_pcm_hw_params_set_channels(m_handle, m_hwParams, _channels); err < 0)
	{
		printf( "Channel count (%i) not available for playbacks: %s\n"
				"(Does your soundcard not support surround?)\n",
					_channels, snd_strerror( err ) );
		return err;
	}

	// set the sample rate
	if (int err = snd_pcm_hw_params_set_rate(m_handle, m_hwParams, sampleRate(), 0); err < 0)
	{
		if (int err = snd_pcm_hw_params_set_rate(m_handle, m_hwParams, audioEngine()->baseSampleRate(), 0); err < 0)
		{
			printf( "Could not set sample rate: %s\n",
							snd_strerror( err ) );
			return err;
		}
	}

	m_periodSize = audioEngine()->framesPerPeriod();
	m_bufferSize = m_periodSize * 8;
	int dir;
	if (int err = snd_pcm_hw_params_set_period_size_near(m_handle, m_hwParams, &m_periodSize, &dir); err < 0)
	{
		printf( "Unable to set period size %lu for playback: %s\n",
					m_periodSize, snd_strerror( err ) );
		return err;
	}
	dir = 0;
	if (int err = snd_pcm_hw_params_get_period_size(m_hwParams, &m_periodSize, &dir); err < 0)
	{
		printf( "Unable to get period size for playback: %s\n",
							snd_strerror( err ) );
	}

	dir = 0;
	if (int err = snd_pcm_hw_params_set_buffer_size_near(m_handle, m_hwParams, &m_bufferSize); err < 0)
	{
		printf( "Unable to set buffer size %lu for playback: %s\n",
					m_bufferSize, snd_strerror( err ) );
		return ( err );
	}

	if (int err = snd_pcm_hw_params_get_buffer_size(m_hwParams, &m_bufferSize); 2 * m_periodSize > m_bufferSize)
	{
		printf( "buffer to small, could not use\n" );
		return ( err );
	}


	// write the parameters to device
	if (int err = snd_pcm_hw_params(m_handle, m_hwParams); err < 0)
	{
		printf( "Unable to set hw params for playback: %s\n",
							snd_strerror( err ) );
		return ( err );
	}

	return ( 0 );	// all ok
}




int AudioAlsa::setSWParams()
{
	// get the current swparams
	if (int err = snd_pcm_sw_params_current(m_handle, m_swParams); err < 0)
	{
		printf( "Unable to determine current swparams for playback: %s"
						"\n", snd_strerror( err ) );
		return err;
	}

	// start the transfer when a period is full
	if (int err = snd_pcm_sw_params_set_start_threshold(m_handle, m_swParams, m_periodSize); err < 0)
	{
		printf( "Unable to set start threshold mode for playback: %s\n",
							snd_strerror( err ) );
		return err;
	}

	// allow the transfer when at least m_periodSize samples can be
	// processed
	if (int err = snd_pcm_sw_params_set_avail_min(m_handle, m_swParams, m_periodSize); err < 0)
	{
		printf( "Unable to set avail min for playback: %s\n",
							snd_strerror( err ) );
		return err;
	}

	// align all transfers to 1 sample
	
#if SND_LIB_VERSION < ((1<<16)|(0)|16)
	if( ( err = snd_pcm_sw_params_set_xfer_align( m_handle,
							m_swParams, 1 ) ) < 0 )
	{
		printf( "Unable to set transfer align for playback: %s\n",
							snd_strerror( err ) );
		return err;
	}
#endif

	// write the parameters to the playback device
	if (int err = snd_pcm_sw_params(m_handle, m_swParams); err < 0)
	{
		printf( "Unable to set sw params for playback: %s\n",
							snd_strerror( err ) );
		return err;
	}

	return 0;	// all ok
}

} // namespace mxm

#endif // MXM_HAVE_ALSA
