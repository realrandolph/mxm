/*
 * AudioSdl.cpp - device-class that performs PCM-output via SDL
 *
 * Copyright (c) 2004-2009 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "AudioSdl.h"
#include "MxmTypes.h"

#ifdef MXM_HAVE_SDL

#include <QFormLayout>
#include <QComboBox>
#include <QLabel>
#include <SDL.h>

#include <cstring>

#include "AudioBufferView.h"
#include "AudioEngine.h"
#include "ConfigManager.h"
#include "mxm_constants.h"

namespace mxm
{

struct AudioSdl::TrackCapture
{
	AudioEngine* engine = nullptr;
	quint64 trackId = 0;
	SDL_AudioDeviceID device = 0;
	int inputChannels = 0;
};

constexpr auto SectionSDL = "audiosdl";
constexpr auto PlaybackDeviceSDL = "device";

namespace
{
auto resolveSdlDeviceName(const QString& selectedDevice, int capture) -> QString
{
	if (!selectedDevice.isEmpty()) { return selectedDevice; }

#if SDL_VERSION_ATLEAST(2, 24, 0)
	char* defaultName = nullptr;
	SDL_AudioSpec defaultSpec{};
	if (SDL_GetDefaultAudioInfo(&defaultName, &defaultSpec, capture) == 0 && defaultName)
	{
		const auto name = QString::fromUtf8(defaultName);
		SDL_free(defaultName);
		return name;
	}
	SDL_free(defaultName);
#endif

	// SDL versions without SDL_GetDefaultAudioInfo cannot identify which
	// enumerated device is the system default. Keep the route unknown rather
	// than treating device index zero as the default.
	return {};
}
} // namespace

AudioSdl::AudioSdl(bool& _success_ful, AudioEngine* _audioEngine)
	: AudioDevice(DEFAULT_CHANNELS, _audioEngine)
	, m_outputDevice(0)
{
	_success_ful = false;

	if( SDL_Init( SDL_INIT_AUDIO | SDL_INIT_EVENTS | SDL_INIT_NOPARACHUTE ) < 0 )
	{
		qCritical( "Couldn't initialize SDL: %s\n", SDL_GetError() );
		return;
	}

	m_configuredPlaybackDevice = ConfigManager::inst()->value(SectionSDL, PlaybackDeviceSDL);
	const auto playbackDevice = m_configuredPlaybackDevice.toStdString();
	if (ConfigManager::inst()->value("audioengine", "samplerateauto", "1").toInt())
	{
#if SDL_VERSION_ATLEAST(2, 0, 16)
		SDL_AudioSpec preferredSpec{};
		bool havePreferredSpec = false;
		if (playbackDevice.empty())
		{
	#if SDL_VERSION_ATLEAST(2, 24, 0)
			havePreferredSpec = SDL_GetDefaultAudioInfo(nullptr, &preferredSpec, 0) == 0 && preferredSpec.freq > 0;
	#endif
		}

		if (!havePreferredSpec)
		{
			const auto numPlaybackDevices = SDL_GetNumAudioDevices(0);
			int preferredDevice = -1;
			if (playbackDevice.empty() && numPlaybackDevices > 0)
			{
				preferredDevice = 0;
			}
			else
			{
				for (int i = 0; i < numPlaybackDevices; ++i)
				{
					const auto* deviceName = SDL_GetAudioDeviceName(i, 0);
					if (deviceName && playbackDevice == deviceName)
					{
						preferredDevice = i;
						break;
					}
				}
			}
			if (preferredDevice < 0)
			{
	#if SDL_VERSION_ATLEAST(2, 24, 0)
				havePreferredSpec = SDL_GetDefaultAudioInfo(nullptr, &preferredSpec, 0) == 0 &&
					preferredSpec.freq > 0;
			#endif
				if (!havePreferredSpec && numPlaybackDevices > 0) { preferredDevice = 0; }
			}
			if (preferredDevice >= 0)
			{
				havePreferredSpec = SDL_GetAudioDeviceSpec(preferredDevice, 0, &preferredSpec) == 0;
			}
		}

		if (havePreferredSpec && preferredSpec.freq > 0)
		{
			_audioEngine->setBaseSampleRate(preferredSpec.freq);
			setSampleRate(preferredSpec.freq);
			ConfigManager::inst()->setValue("audioengine", "samplerate", QString::number(preferredSpec.freq));
		}
#endif
	}

	m_audioHandle.freq = sampleRate();
	m_audioHandle.format = AUDIO_F32SYS;	// we want it in byte-order
						// of system, so we don't have
						// to convert the buffers

	m_audioHandle.channels = channels();
	m_audioHandle.samples = _audioEngine->framesPerAudioBuffer();

	m_audioHandle.callback = sdlAudioCallback;
	m_audioHandle.userdata = this;

  	SDL_AudioSpec actual; 

	const bool isDefaultPlayback = playbackDevice.empty();

	// Try with the configured device
	const auto playbackDeviceCStr = isDefaultPlayback ? nullptr : playbackDevice.c_str();
	m_outputDevice = SDL_OpenAudioDevice(playbackDeviceCStr, 0, &m_audioHandle, &actual, 0);
	const auto configuredPlaybackOpened = m_outputDevice != 0;

	// If we did not get a device ID try again with the default device if we did not try that before
	if (m_outputDevice == 0 && !isDefaultPlayback)
	{
		m_outputDevice = SDL_OpenAudioDevice(nullptr, 0, &m_audioHandle, &actual, 0);
	}

	if (m_outputDevice == 0) {
		qCritical( "Couldn't open SDL-audio: %s\n", SDL_GetError() );
		return;
	}
	m_activePlaybackUsesDefault = isDefaultPlayback || !configuredPlaybackOpened;
	m_activePlaybackDevice = m_activePlaybackUsesDefault
		? QString{} : m_configuredPlaybackDevice;

	_success_ful = true;

	// Workaround for a race condition that causes SDL to segfault
	SDL_Delay(50);
	m_supportsCapture = SDL_GetNumAudioDevices(1) > 0;

	m_monitoredPlaybackDevice = resolveSdlDeviceName(
		m_activePlaybackUsesDefault ? QString{} : m_activePlaybackDevice, 0);
	SDL_EventState(SDL_AUDIODEVICEREMOVED, SDL_ENABLE);
	SDL_AddEventWatch(&AudioSdl::sdlAudioEventWatch, this);
	QObject::connect(&m_deviceMonitor, &QTimer::timeout, [this] { checkDeviceRoutes(); });
	m_deviceMonitor.start(1000);
}




AudioSdl::~AudioSdl()
{
	m_deviceMonitor.stop();
	SDL_DelEventWatch(&AudioSdl::sdlAudioEventWatch, this);
	for (auto& [id, capture] : m_trackCaptures)
	{
		Q_UNUSED(id);
		if (capture->device != 0) { SDL_CloseAudioDevice(capture->device); }
	}
	if (m_outputDevice != 0)
		SDL_CloseAudioDevice(m_outputDevice);

	SDL_Quit();
}

auto AudioSdl::inputSources() const -> std::vector<AudioInputSource>
{
	auto sources = std::vector<AudioInputSource>{};
	if (m_supportsCapture)
	{
		sources.push_back(makeAudioInputSource(
			QString::fromLatin1(SystemDefaultInputSourceId), QObject::tr("System Default"), 2,
			{QString::fromLatin1(SystemDefaultInputSourceId)}, true));
	}
	const auto count = SDL_GetNumAudioDevices(1);
	for (int index = 0; index < count; ++index)
	{
		const auto* name = SDL_GetAudioDeviceName(index, 1);
		if (!name || !*name) { continue; }
		const auto deviceName = QString::fromUtf8(name);
		sources.push_back(makeAudioInputSource(
			QStringLiteral("sdl:") + deviceName,
			deviceName,
			2,
			{deviceName}, true));
	}
	return sources;
}

auto AudioSdl::registerTrackInputPort(quint64 id, const QString&, ch_cnt_t channels) -> bool
{
	if (id == 0 || channels != 2) { return false; }
	auto capture = std::make_unique<TrackCapture>();
	capture->engine = audioEngine();
	capture->trackId = id;
	return m_trackCaptures.emplace(id, std::move(capture)).second;
}

void AudioSdl::unregisterTrackInputPort(quint64 id)
{
	const auto found = m_trackCaptures.find(id);
	if (found == m_trackCaptures.end()) { return; }
	if (found->second->device != 0) { SDL_CloseAudioDevice(found->second->device); }
	m_trackCaptures.erase(found);
}

void AudioSdl::setTrackInputSource(quint64 id, const QStringList& endpointPorts)
{
	const auto found = m_trackCaptures.find(id);
	if (found == m_trackCaptures.end()) { return; }
	auto& capture = *found->second;
	if (capture.device != 0)
	{
		SDL_CloseAudioDevice(capture.device);
		capture.device = 0;
	}
	if (endpointPorts.isEmpty())
	{
		audioEngine()->setTrackInputCaptureFailed(id, false);
		return;
	}

	SDL_AudioSpec desired{};
	desired.freq = sampleRate();
	desired.format = AUDIO_F32SYS;
	desired.channels = 2;
	desired.samples = static_cast<Uint16>(audioEngine()->framesPerAudioBuffer());
	desired.callback = sdlTrackInputCallback;
	desired.userdata = &capture;
	const auto useDefault = endpointPorts.front() == QLatin1String(SystemDefaultInputSourceId);
	const auto deviceName = endpointPorts.front().toUtf8();
	SDL_AudioSpec actual{};
	capture.device = SDL_OpenAudioDevice(useDefault ? nullptr : deviceName.constData(), 1, &desired, &actual,
		SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
	if (capture.device == 0 || actual.format != AUDIO_F32SYS || actual.channels == 0
		|| actual.channels > MaxChannelsPerAudioBuffer)
	{
		qWarning("Couldn't open SDL capture device '%s': %s", deviceName.constData(), SDL_GetError());
		if (capture.device != 0) { SDL_CloseAudioDevice(capture.device); }
		capture.device = 0;
		audioEngine()->setTrackInputCaptureFailed(id, true);
		return;
	}
	capture.inputChannels = actual.channels;
	audioEngine()->setTrackInputChannels(id, capture.inputChannels);
	audioEngine()->setTrackInputCaptureFailed(id, false);
	if (isRunning()) { SDL_PauseAudioDevice(capture.device, 0); }
}

void AudioSdl::sdlTrackInputCallback(void* data, Uint8* buffer, int length)
{
	auto& capture = *static_cast<TrackCapture*>(data);
	if (length <= 0 || capture.inputChannels <= 0) { return; }
	const auto frames = static_cast<f_cnt_t>(length / (sizeof(float) * capture.inputChannels));
	capture.engine->pushTrackInputFrames(capture.trackId,
		InterleavedBufferView<const float>{reinterpret_cast<const float*>(buffer),
			static_cast<ch_cnt_t>(capture.inputChannels), frames});
}

void AudioSdl::checkDeviceRoutes()
{
	SDL_PumpEvents();

	if (m_playbackDeviceRemoved.exchange(false, std::memory_order_acq_rel)
		&& !m_playbackRemovalReported.exchange(true, std::memory_order_acq_rel))
	{
		reportDeviceChange(false, m_monitoredPlaybackDevice, QString{}, false);
	}

}

int AudioSdl::sdlAudioEventWatch(void* data, SDL_Event* event)
{
	if (!event || event->type != SDL_AUDIODEVICEREMOVED) { return 1; }

	auto* const device = static_cast<AudioSdl*>(data);
	if (!event->adevice.iscapture && device->m_outputDevice != 0 && event->adevice.which == device->m_outputDevice)
	{
		device->m_playbackDeviceRemoved.store(true, std::memory_order_release);
	}
	return 1;
}




void AudioSdl::startProcessingImpl()
{
	if (m_outputDevice != 0) { SDL_PauseAudioDevice(m_outputDevice, 0); }
	for (auto& [id, capture] : m_trackCaptures)
	{
		Q_UNUSED(id);
		if (capture->device != 0) { SDL_PauseAudioDevice(capture->device, 0); }
	}
}




void AudioSdl::stopProcessingImpl()
{
	if (m_outputDevice != 0 && SDL_GetAudioDeviceStatus(m_outputDevice) == SDL_AUDIO_PLAYING)
	{
		SDL_LockAudioDevice(m_outputDevice);
		SDL_PauseAudioDevice(m_outputDevice, 1);
		SDL_UnlockAudioDevice(m_outputDevice);
	}
	for (auto& [id, capture] : m_trackCaptures)
	{
		Q_UNUSED(id);
		if (capture->device != 0) { SDL_PauseAudioDevice(capture->device, 1); }
	}
}

void AudioSdl::sdlAudioCallback( void * _udata, Uint8 * _buf, int _len )
{
	auto _this = static_cast<AudioSdl*>(_udata);

	_this->sdlAudioCallback( _buf, _len );
}




void AudioSdl::sdlAudioCallback( Uint8 * _buf, int _len )
{
	if (!isRunning())
	{
		memset( _buf, 0, _len );
		return;
	}

	const auto frameCount = _len / sizeof(float) / channels();
	audioEngine()->renderNextBuffer({reinterpret_cast<float*>(_buf), channels(), frameCount});
}

QString AudioSdl::setupWidget::s_systemDefaultDevice = AudioDeviceSetupWidget::tr("[System Default]");

AudioSdl::setupWidget::setupWidget( QWidget * _parent ) :
	AudioDeviceSetupWidget( AudioSdl::name(), _parent )
{
	QFormLayout * form = new QFormLayout(this);
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);

	m_playbackDeviceComboBox = new QComboBox(this);

	populatePlaybackDeviceComboBox();

	form->addRow(tr("Playback device"), m_playbackDeviceComboBox);

	m_playbackDeviceComboBox->setMinimumHeight(m_playbackDeviceComboBox->sizeHint().height());

	QObject::connect(m_playbackDeviceComboBox, qOverload<int>(&QComboBox::currentIndexChanged),
		this, [this] { updateDefaultDeviceInfo(); });
	QObject::connect(&m_defaultDeviceInfoTimer, &QTimer::timeout,
		this, [this] { updateDefaultDeviceInfo(); });
}




void AudioSdl::setupWidget::saveSettings()
{
	ConfigManager::inst()->setValue(
		SectionSDL, PlaybackDeviceSDL, m_playbackDeviceComboBox->currentData().toString());
}

void AudioSdl::setupWidget::populatePlaybackDeviceComboBox()
{
	m_playbackDeviceComboBox->addItem(s_systemDefaultDevice, QString{});

	QStringList playbackDevices;
	const int numberOfPlaybackDevices = SDL_GetNumAudioDevices(0);
	for (int i = 0; i < numberOfPlaybackDevices; ++i)
	{
		const QString deviceName = SDL_GetAudioDeviceName(i, 0);
		playbackDevices.append(deviceName);
	}

	playbackDevices.sort();

	for (const auto& device : playbackDevices) { m_playbackDeviceComboBox->addItem(device, device); }

	const auto playbackDevice = ConfigManager::inst()->value(SectionSDL, PlaybackDeviceSDL);
	const auto index = m_playbackDeviceComboBox->findData(playbackDevice);
	m_playbackDeviceComboBox->setCurrentIndex(index < 0 ? 0 : index);
}

void AudioSdl::setupWidget::updateDefaultDeviceInfo()
{
	const auto updateCombo = [this](QComboBox* combo) {
		const auto defaultIndex = combo->findData(QString{});
		if (defaultIndex < 0) { return false; }
		if (!isVisible() || combo->currentIndex() != defaultIndex)
		{
			if (combo->itemText(defaultIndex) != s_systemDefaultDevice)
			{
				combo->setItemText(defaultIndex, s_systemDefaultDevice);
			}
			return false;
		}

		const auto resolved = resolveSdlDeviceName({}, 0);
		const auto text = resolved.isEmpty()
			? s_systemDefaultDevice
			: QStringLiteral("%1 [%2]").arg(s_systemDefaultDevice, resolved);
		if (combo->itemText(defaultIndex) != text) { combo->setItemText(defaultIndex, text); }
		return true;
	};

	const auto playbackDefault = updateCombo(m_playbackDeviceComboBox);
	if (playbackDefault)
	{
		if (!m_defaultDeviceInfoTimer.isActive()) { m_defaultDeviceInfoTimer.start(30000); }
	}
	else { m_defaultDeviceInfoTimer.stop(); }
}

void AudioSdl::setupWidget::showEvent(QShowEvent* event)
{
	QGroupBox::showEvent(event);
	updateDefaultDeviceInfo();
}

void AudioSdl::setupWidget::hideEvent(QHideEvent* event)
{
	m_defaultDeviceInfoTimer.stop();
	QGroupBox::hideEvent(event);
}


} // namespace mxm

#endif // MXM_HAVE_SDL
