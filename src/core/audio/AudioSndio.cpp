/*
 * AudioSndio.cpp - base-class that implements sndio audio support
 *
 * Copyright (c) 2010-2016 jackmsr@openbsd.net
 * Copyright (c) 2016-2017 David Carlier <devnexen@gmail.com>
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

#include "AudioSndio.h"

#ifdef MXM_HAVE_SNDIO

#include <poll.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <functional>
#include <optional>
#include <thread>
#include <vector>

#include <QFormLayout>
#include <QLineEdit>

#include "endian_handling.h"
#include "LcdSpinBox.h"
#include "AudioBufferView.h"
#include "AudioEngine.h"

#include "ConfigManager.h"


namespace mxm
{

struct AudioSndio::TrackCapture
{
	AudioEngine* engine = nullptr;
	quint64 trackId = 0;
	std::optional<QString> device;
	std::atomic_bool stop{false};
	std::thread reader;
};

namespace
{

constexpr auto CaptureChannels = ch_cnt_t{2};

auto configureCapture(sio_hdl* handle, sample_rate_t rate, f_cnt_t periodFrames,
	ch_cnt_t channels) -> bool
{
	struct sio_par parameters;
	sio_initpar(&parameters);
	parameters.rchan = channels;
	parameters.bits = 16;
	parameters.sig = 1;
	parameters.le = SIO_LE_NATIVE;
	parameters.rate = rate;
	parameters.round = std::max<f_cnt_t>(periodFrames, 1);
	parameters.appbufsz = parameters.round * 4;

	if (!sio_setpar(handle, &parameters)) { return false; }
	if (!sio_getpar(handle, &parameters)) { return false; }
	return parameters.rchan == channels
		&& parameters.rate == static_cast<unsigned int>(rate)
		&& parameters.bits == 16
		&& parameters.sig == 1
		&& parameters.le == SIO_LE_NATIVE;
}

auto openCaptureStream(const QString& device, sample_rate_t rate, f_cnt_t periodFrames,
	ch_cnt_t& channels) -> sio_hdl*
{
	const auto name = device.toLatin1();
	const auto* path = device.isEmpty() ? nullptr : name.constData();

	for (const auto candidate : {CaptureChannels, ch_cnt_t{1}})
	{
		auto* handle = sio_open(path, SIO_REC, 0);
		if (handle == nullptr) { return nullptr; }
		if (configureCapture(handle, rate, periodFrames, candidate) && sio_start(handle))
		{
			channels = candidate;
			return handle;
		}
		sio_close(handle);
	}

	return nullptr;
}

auto capturesAudio(const QString& device, sample_rate_t rate, f_cnt_t periodFrames) -> bool
{
	auto channels = CaptureChannels;
	auto* handle = openCaptureStream(device, rate, periodFrames, channels);
	if (handle == nullptr) { return false; }
	sio_stop(handle);
	sio_close(handle);
	return true;
}

} // namespace

AudioSndio::AudioSndio(bool& _success_ful, AudioEngine* _audioEngine)
	: AudioDevice(std::clamp<ch_cnt_t>(ConfigManager::inst()->value("audiosndio", "channels").toInt(), DEFAULT_CHANNELS,
					  DEFAULT_CHANNELS),
		  _audioEngine)
{
	_success_ful = false;

	QString dev = ConfigManager::inst()->value( "audiosndio", "device" );
	m_configuredDevice = dev;
	m_monitoredDevice = dev.isEmpty() ? QStringLiteral("default") : dev;

	if (dev == "")
	{
		m_hdl = sio_open( nullptr, SIO_PLAY, 0 );
	}
	else
	{
		m_hdl = sio_open( dev.toLatin1().constData(), SIO_PLAY, 0 );
	}

	if( m_hdl == nullptr )
	{
		printf( "sndio: failed opening audio-device\n" );
		return;
	}

	sio_initpar(&m_par);

	m_par.pchan = channels();
	m_par.bits = sizeof(int_sample_t) * 8;
	m_par.sig = 1; // int_sample_t must be signed
	m_par.le = SIO_LE_NATIVE;
	m_par.rate = sampleRate();
	m_par.round = audioEngine()->framesPerAudioBuffer();
	m_par.appbufsz = m_par.round * 2;

	struct sio_par reqpar = m_par;

	if (!sio_setpar(m_hdl, &m_par))
	{
		printf( "sndio: sio_setpar failed\n" );
		return;
	}
	if (!sio_getpar(m_hdl, &m_par))
	{
		printf( "sndio: sio_getpar failed\n" );
		return;
	}

	if (reqpar.pchan != m_par.pchan ||
		reqpar.bits != m_par.bits ||
		reqpar.le != m_par.le ||
		(::abs(static_cast<int>(reqpar.rate) - static_cast<int>(m_par.rate)) * 100)/reqpar.rate > 2)
	{
		printf( "sndio: returned params not as requested\n" );
		return;
	}

	if (!sio_start(m_hdl))
	{
		printf( "sndio: sio_start failed\n" );
		return;
	}

	// sndio offers no device enumeration, so the configured playback device is
	// also the only capture endpoint that can be selected honestly.
	m_supportsCapture = capturesAudio(m_configuredDevice, sampleRate(),
		audioEngine()->framesPerAudioBuffer());
	_success_ful = true;
}


AudioSndio::~AudioSndio()
{
	stopTrackCaptures();
	if (m_hdl != nullptr)
	{
		sio_close( m_hdl );
		m_hdl = nullptr;
	}
}


auto AudioSndio::supportsTrackInputPorts() const -> bool
{
	return m_supportsCapture;
}




auto AudioSndio::inputSources() const -> std::vector<AudioInputSource>
{
	auto sources = std::vector<AudioInputSource>{};
	if (!m_supportsCapture) { return sources; }

	const auto label = m_configuredDevice.isEmpty()
		? AudioSndio::tr("System Default input") : m_configuredDevice;
	const auto idSuffix = m_configuredDevice.isEmpty()
		? QStringLiteral("system-default") : m_configuredDevice;
	sources.push_back(AudioInputSource{
		QStringLiteral("sndio-rec:") + idSuffix,
		label,
		CaptureChannels,
		AudioChannelLayout::stereo(),
		{0, 1},
		true,
		{m_configuredDevice.isEmpty() ? QStringLiteral("system-default") : m_configuredDevice},
	});
	return sources;
}




auto AudioSndio::registerTrackInputPort(quint64 id, const QString& name, ch_cnt_t channels) -> bool
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




void AudioSndio::unregisterTrackInputPort(quint64 id)
{
	const auto found = m_trackCaptures.find(id);
	if (found == m_trackCaptures.end()) { return; }
	stopTrackCapture(*found->second);
	m_trackCaptures.erase(found);
}




void AudioSndio::setTrackInputSource(quint64 id, const QStringList& endpointPorts)
{
	const auto found = m_trackCaptures.find(id);
	if (found == m_trackCaptures.end()) { return; }

	auto& capture = *found->second;
	stopTrackCapture(capture);
	capture.device.reset();
	if (endpointPorts.isEmpty()) { return; }

	const auto endpoint = endpointPorts.front();
	capture.device = endpoint == QStringLiteral("system-default") ? QString{} : endpoint;
	if (AudioDevice::isRunning()) { startTrackCapture(capture); }
}




void AudioSndio::startTrackCapture(TrackCapture& capture)
{
	if (!capture.device || capture.reader.joinable()) { return; }
	capture.stop.store(false, std::memory_order_release);
	capture.reader = std::thread(&AudioSndio::captureThread, this, std::ref(capture));
}




void AudioSndio::stopTrackCapture(TrackCapture& capture)
{
	capture.stop.store(true, std::memory_order_release);
	if (capture.reader.joinable()) { capture.reader.join(); }
}




void AudioSndio::stopTrackCaptures()
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




void AudioSndio::captureThread(TrackCapture& capture)
{
	const auto framesPerRead = std::max<f_cnt_t>(audioEngine()->framesPerAudioBuffer(), 1);
	if (!capture.device) { return; }

	auto channels = CaptureChannels;
	auto* handle = openCaptureStream(*capture.device, sampleRate(), framesPerRead, channels);
	if (handle == nullptr)
	{
		qWarning("sndio: cannot open capture device '%s'",
			qUtf8Printable(capture.device->isEmpty() ? QStringLiteral("default") : *capture.device));
		return;
	}

	std::vector<int16_t> integerFrames(static_cast<std::size_t>(framesPerRead) * channels);
	std::vector<float> interleaved(integerFrames.size());

	auto descriptorCount = sio_nfds(handle);
	if (descriptorCount <= 0) { descriptorCount = 1; }
	std::vector<pollfd> descriptors(static_cast<std::size_t>(descriptorCount));

	while (!capture.stop.load(std::memory_order_acquire) && AudioDevice::isRunning())
	{
		const auto count = sio_pollfd(handle, descriptors.data(), descriptorCount);
		if (count <= 0) { break; }

		const auto ready = poll(descriptors.data(), static_cast<nfds_t>(count), 50);
		if (ready < 0)
		{
			if (errno == EINTR) { continue; }
			break;
		}
		if (ready == 0) { continue; }

		const auto events = sio_revents(handle, descriptors.data());
		if (events & (POLLHUP | POLLERR)) { break; }
		if (!(events & POLLIN)) { continue; }

		const auto bytes = sio_read(handle, integerFrames.data(),
			integerFrames.size() * sizeof(int16_t));
		if (bytes == 0)
		{
			if (sio_eof(handle)) { break; }
			continue;
		}

		const auto frames = static_cast<f_cnt_t>(
			bytes / (sizeof(int16_t) * static_cast<std::size_t>(channels)));
		if (frames == 0) { continue; }

		const auto samples = static_cast<std::size_t>(frames) * channels;
		for (std::size_t sample = 0; sample < samples; ++sample)
		{
			interleaved[sample] = static_cast<float>(integerFrames[sample]) / 32767.0f;
		}

		capture.engine->pushTrackInputFrames(capture.trackId,
			InterleavedBufferView<const float>{interleaved.data(), channels, frames});
	}

	sio_stop(handle);
	sio_close(handle);
}




void AudioSndio::startProcessingImpl()
{
	start(QThread::HighPriority);
	for (auto& [id, capture] : m_trackCaptures)
	{
		Q_UNUSED(id);
		startTrackCapture(*capture);
	}
}


void AudioSndio::stopProcessingImpl()
{
	stopTrackCaptures();
	stopProcessingThread( this );
}

void AudioSndio::run()
{
	const auto framesPerAudioBuffer = audioEngine()->framesPerAudioBuffer();
	const auto samplesPerAudioBuffer = framesPerAudioBuffer * channels();
	auto fbuf = std::vector<sample_t>(samplesPerAudioBuffer);
	auto ibuf = std::vector<int_sample_t>(samplesPerAudioBuffer);

	while (AudioDevice::isRunning())
	{
		audioEngine()->renderNextBuffer({fbuf.data(), channels(), framesPerAudioBuffer});

		// Sndio doesn't speak float, so convert samples to signed int.
		// While convertToS16() exists, it is intentionally not used
		// here. There is no need to convert endian-ness since sndio was
		// initialized with SIO_LE_NATIVE, and there's no reason to
		// also convert to SampleFrame.
		for (auto i = 0u; i < samplesPerAudioBuffer; ++i)
		{
			ibuf[i] = static_cast<int_sample_t>(AudioEngine::clip(fbuf[i]) * OUTPUT_SAMPLE_MULTIPLIER);
		}

		const auto written = sio_write(m_hdl, ibuf.data(), ibuf.size() * sizeof(int_sample_t));
		if (written == 0 || sio_eof(m_hdl))
		{
			if (AudioDevice::isRunning() && !m_errorReported.exchange(true, std::memory_order_acq_rel))
			{
				reportDeviceChange(false, m_monitoredDevice, QString{}, false);
			}
			break;
		}
	}
}


// Note: sndio does not provide an API to enumerate audio devices or resolve hardware names.
// It relies on the AUDIODEVICE environment variable or the system default ("default").
QString AudioSndio::resolvedDefaultDevice()
{
	const char* envDev = getenv("AUDIODEVICE");
	if (envDev != nullptr && *envDev != '\0')
	{
		return QString::fromLatin1(envDev);
	}
	return QString{};
}

AudioSndio::setupWidget::setupWidget( QWidget * _parent ) :
	AudioDeviceSetupWidget( AudioSndio::name(), _parent )
{
	QFormLayout * form = new QFormLayout(this);

	const auto dev = ConfigManager::inst()->value( "audiosndio", "device" );
	const auto resolvedDefault = AudioSndio::resolvedDefaultDevice();
	const auto defaultText = resolvedDefault.isEmpty()
		? tr("[System Default]")
		: QStringLiteral("%1 [%2]").arg(tr("[System Default]"), resolvedDefault);

	m_device = new QLineEdit( dev == QStringLiteral("default") ? QString{} : dev, this );
	m_device->setPlaceholderText( defaultText );
	form->addRow(tr("Device"), m_device);

	gui::LcdSpinBoxModel * m = new gui::LcdSpinBoxModel( /* this */ );
	m->setRange(DEFAULT_CHANNELS, DEFAULT_CHANNELS);
	m->setStep( 2 );
	m->setValue( ConfigManager::inst()->value( "audiosndio",
	    "channels" ).toInt() );

	m_channels = new gui::LcdSpinBox( 1, this );
	m_channels->setModel( m );

	form->addRow(tr("Channels"), m_channels);
}


void AudioSndio::setupWidget::saveSettings()
{
	ConfigManager::inst()->setValue( "audiosndio", "device",
	    m_device->text() );
	ConfigManager::inst()->setValue( "audiosndio", "channels",
	    QString::number( m_channels->value<int>() ) );
}


} // namespace mxm

#endif	// MXM_HAVE_SNDIO
