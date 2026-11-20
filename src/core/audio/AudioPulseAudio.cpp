/*
 * AudioPulseAudio.cpp - device-class which implements PulseAudio-output
 *
 * Copyright (c) 2008-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include <QFormLayout>
#include <QLineEdit>
#include <QShowEvent>

#include "AudioPulseAudio.h"

#ifdef MXM_HAVE_PULSEAUDIO

#include "ConfigManager.h"
#include "LcdSpinBox.h"
#include "AudioEngine.h"
#include "Engine.h"

namespace mxm
{

static void stream_write_callback(pa_stream *s, size_t length, void *userdata)
{
	static_cast<AudioPulseAudio *>( userdata )->streamWriteCallback( s, length );
}

AudioPulseAudio::AudioPulseAudio(bool& _success_ful, AudioEngine* _audioEngine)
	: AudioDevice(std::clamp<ch_cnt_t>(
					  ConfigManager::inst()->value("audiopa", "channels").toInt(), DEFAULT_CHANNELS, DEFAULT_CHANNELS),
		  _audioEngine)
	, m_s(nullptr)
	, m_latency(static_cast<double>(_audioEngine->framesPerAudioBuffer()) / sampleRate())
{
	_success_ful = false;

	m_sampleSpec.format = PA_SAMPLE_FLOAT32;
	m_sampleSpec.rate = sampleRate();
	m_sampleSpec.channels = channels();

	m_monitoredDevice = probeDevice();
	_success_ful = true;
}

QString AudioPulseAudio::probeDevice()
{
	QString dev = ConfigManager::inst()->value( "audiopa", "device" );
	if( dev.isEmpty() )
	{
		if( getenv( "AUDIODEV" ) != nullptr )
		{
			return getenv( "AUDIODEV" );
		}
		return "default";
	}
	return dev;
}

QString AudioPulseAudio::resolvedDefaultDevice()
{
	if (getenv("PULSE_SINK") != nullptr)
	{
		return QString::fromLatin1(getenv("PULSE_SINK"));
	}
	if (getenv("AUDIODEV") != nullptr)
	{
		return QString::fromLatin1(getenv("AUDIODEV"));
	}
	if (Engine::audioEngine() && Engine::audioEngine()->audioDev())
	{
		auto* activeDev = dynamic_cast<AudioPulseAudio*>(Engine::audioEngine()->audioDev());
		if (activeDev)
		{
			const auto device = activeDev->monitoredDevice();
			if (!device.isEmpty() && device != QStringLiteral("default")) { return device; }
		}
	}

	return QStringLiteral("default");
}

QString AudioPulseAudio::monitoredDevice() const
{
	const std::lock_guard<std::mutex> lock(m_monitoredDeviceMutex);
	return m_monitoredDevice;
}

void AudioPulseAudio::setMonitoredDevice(const QString& device)
{
	const std::lock_guard<std::mutex> lock(m_monitoredDeviceMutex);
	m_monitoredDevice = device;
}




void AudioPulseAudio::startProcessingImpl()
{
	start(QThread::HighPriority);
}




void AudioPulseAudio::stopProcessingImpl()
{
	stopProcessingThread( this );
}


static void stream_moved_callback(pa_stream* s, void* userdata)
{
	static_cast<AudioPulseAudio*>(userdata)->streamMovedCallback(s);
}

void AudioPulseAudio::streamMovedCallback(pa_stream* s)
{
	const char* sinkName = pa_stream_get_device_name(s);
	const QString newDevice = sinkName ? QString::fromUtf8(sinkName) : QString{};
	const auto previousDevice = monitoredDevice();
	if (!newDevice.isEmpty() && newDevice != previousDevice)
	{
		setMonitoredDevice(newDevice);
		reportDeviceChange(false, previousDevice, newDevice, true);
	}
}

static void stream_state_callback( pa_stream *s, void * userdata )
{
	static_cast<AudioPulseAudio*>(userdata)->streamStateCallback(s);
}

void AudioPulseAudio::streamStateCallback( pa_stream *s )
{
	switch( pa_stream_get_state( s ) )
	{
		case PA_STREAM_CREATING:
		case PA_STREAM_TERMINATED:
			break;

		case PA_STREAM_READY:
		{
			qDebug( "Stream successfully created\n" );
			const char* sinkName = pa_stream_get_device_name(s);
			if (sinkName)
			{
				setMonitoredDevice(QString::fromUtf8(sinkName));
			}
			break;
		}

		case PA_STREAM_FAILED:
		default:
		{
			qCritical( "Stream error: %s\n",
					pa_strerror(pa_context_errno(
						pa_stream_get_context( s ) ) ) );
			if (AudioDevice::isRunning() && !m_streamFailureReported.exchange(true, std::memory_order_acq_rel))
			{
				reportDeviceChange(false, monitoredDevice(), QString{}, false);
			}
			break;
		}
	}
}



static void context_state_callback(pa_context *c, void *userdata)
{
	static_cast<AudioPulseAudio*>(userdata)->contextStateCallback(c);
}

void AudioPulseAudio::contextStateCallback(pa_context *c)
{
	switch( pa_context_get_state( c ) )
	{
		case PA_CONTEXT_CONNECTING:
		case PA_CONTEXT_AUTHORIZING:
		case PA_CONTEXT_SETTING_NAME:
			break;

		case PA_CONTEXT_READY:
		{
			qDebug( "Connection established.\n" );
			m_s = pa_stream_new( c, "mxm", &m_sampleSpec,  nullptr);
			pa_stream_set_state_callback( m_s, stream_state_callback, this );
			pa_stream_set_write_callback( m_s, stream_write_callback, this );
			pa_stream_set_moved_callback( m_s, stream_moved_callback, this );

			pa_buffer_attr buffer_attr;

			buffer_attr.maxlength = (uint32_t)(-1);

			// play silence in case of buffer underrun instead of using default rewind
			buffer_attr.prebuf = 0;

			buffer_attr.minreq = (uint32_t)(-1);
			buffer_attr.fragsize = (uint32_t)(-1);

			// ask PulseAudio for the desired latency (which might not be approved)
			buffer_attr.tlength = pa_usec_to_bytes(m_latency * PA_USEC_PER_MSEC, &m_sampleSpec);

			pa_stream_connect_playback( m_s, nullptr, &buffer_attr,
										PA_STREAM_ADJUST_LATENCY,
										nullptr,	// volume
										nullptr );
			signalConnected( true );
			break;
		}

		case PA_CONTEXT_TERMINATED:
			break;

		case PA_CONTEXT_FAILED:
		default:
		{
			qCritical( "Connection failure: %s\n", pa_strerror( pa_context_errno( c ) ) );
			signalConnected( false );
			if (m_connected && AudioDevice::isRunning() && !m_streamFailureReported.exchange(true, std::memory_order_acq_rel))
			{
				reportDeviceChange(false, monitoredDevice(), QString{}, false);
			}
			break;
		}
	}
}




void AudioPulseAudio::run()
{
	pa_mainloop * mainLoop = pa_mainloop_new();
	if( !mainLoop )
	{
		qCritical( "pa_mainloop_new() failed.\n" );
		return;
	}
	pa_mainloop_api * mainloop_api = pa_mainloop_get_api( mainLoop );

	pa_context *context = pa_context_new( mainloop_api, "mxm" );
	if ( context == nullptr )
	{
		qCritical( "pa_context_new() failed." );
		return;
	}

	m_connected = false;

	pa_context_set_state_callback( context, context_state_callback, this  );
	// connect the context
	pa_context_connect( context, nullptr, (pa_context_flags) 0, nullptr );

	while (!m_connectedSemaphore.tryAcquire()) {
		pa_mainloop_iterate(mainLoop, 1, nullptr);
	}

	// run the main loop
	if( m_connected )
	{
		int ret = 0;
		while (AudioDevice::isRunning() && pa_mainloop_iterate(mainLoop, 1, &ret) >= 0) {}

		pa_stream_disconnect( m_s );
		pa_stream_unref( m_s );
	}
	else
	{
		while (AudioDevice::isRunning())
		{
			audioEngine()->renderNextPeriod();
		}
	}

	pa_context_disconnect( context );
	pa_context_unref( context );

	pa_mainloop_free( mainLoop );
}

void AudioPulseAudio::streamWriteCallback(pa_stream*, size_t)
{
	auto buf = static_cast<void*>(nullptr);
	auto maxBufSizeInBytes = audioEngine()->framesPerAudioBuffer() * channels() * sizeof(float);

	if (pa_stream_begin_write(m_s, &buf, &maxBufSizeInBytes) != 0 || !buf) { return; }

	const auto numSamples = maxBufSizeInBytes / sizeof(float);
	const auto numFrames = numSamples / channels();

	if (!AudioDevice::isRunning())
	{
		std::fill_n(static_cast<float*>(buf), numSamples, 0.f);
	}
	else
	{
		audioEngine()->renderNextBuffer({static_cast<float*>(buf), channels(), numFrames});
	}

	pa_stream_write(m_s, buf, maxBufSizeInBytes, nullptr, 0, PA_SEEK_RELATIVE);
}




void AudioPulseAudio::signalConnected( bool connected )
{
	if( !m_connected )
	{
		m_connected = connected;
		m_connectedSemaphore.release();
	}
}




AudioPulseAudio::setupWidget::setupWidget( QWidget * _parent ) :
	AudioDeviceSetupWidget( AudioPulseAudio::name(), _parent )
{
	QFormLayout * form = new QFormLayout(this);

	const auto dev = ConfigManager::inst()->value( "audiopa", "device" );

	m_device = new QLineEdit( dev == QStringLiteral("default") ? QString{} : dev, this );
	m_device->setPlaceholderText( tr("[System Default]") );
	form->addRow(tr("Device"), m_device);

	auto m = new gui::LcdSpinBoxModel();
	m->setRange(DEFAULT_CHANNELS, DEFAULT_CHANNELS);
	m->setStep( 2 );
	m->setValue( ConfigManager::inst()->value( "audiopa",
										 "channels" ).toInt() );

	m_channels = new gui::LcdSpinBox( 1, this );
	m_channels->setModel( m );

	form->addRow(tr("Channels"), m_channels);
}

void AudioPulseAudio::setupWidget::showEvent( QShowEvent * _event )
{
	AudioDeviceSetupWidget::showEvent(_event);

	const auto resolvedDefault = AudioPulseAudio::resolvedDefaultDevice();
	const auto placeholder = resolvedDefault.isEmpty() || resolvedDefault == QStringLiteral("default")
		? tr("[System Default]")
		: QStringLiteral("%1 [%2]").arg(tr("[System Default]"), resolvedDefault);
	m_device->setPlaceholderText(placeholder);
}




AudioPulseAudio::setupWidget::~setupWidget()
{
	delete m_channels->model();
}




void AudioPulseAudio::setupWidget::saveSettings()
{
	ConfigManager::inst()->setValue( "audiopa", "device",
							m_device->text() );
	ConfigManager::inst()->setValue( "audiopa", "channels",
				QString::number( m_channels->value<int>() ) );
}

} // namespace mxm

#endif // MXM_HAVE_PULSEAUDIO
