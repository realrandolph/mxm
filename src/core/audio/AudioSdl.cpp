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

#include <algorithm>

#include <QFormLayout>
#include <QComboBox>
#include <SDL.h>

#include "AudioEngine.h"
#include "ConfigManager.h"

namespace mxm
{

constexpr auto SectionSDL = "audiosdl";
constexpr auto PlaybackDeviceSDL = "device";
constexpr auto InputDeviceSDL = "inputdevice";

AudioSdl::AudioSdl(bool& _success_ful, AudioEngine* _audioEngine)
	: AudioDevice(DEFAULT_CHANNELS, _audioEngine)
	, m_outputDevice(0)
	, m_inputDevice(0)
{
	_success_ful = false;

	if( SDL_Init( SDL_INIT_AUDIO | SDL_INIT_NOPARACHUTE ) < 0 )
	{
		qCritical( "Couldn't initialize SDL: %s\n", SDL_GetError() );
		return;
	}

	const auto playbackDevice = ConfigManager::inst()->value(SectionSDL, PlaybackDeviceSDL).toStdString();
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

	// If we did not get a device ID try again with the default device if we did not try that before
	if (m_outputDevice == 0 && !isDefaultPlayback)
	{
		m_outputDevice = SDL_OpenAudioDevice(nullptr, 0, &m_audioHandle, &actual, 0);
	}

	if (m_outputDevice == 0) {
		qCritical( "Couldn't open SDL-audio: %s\n", SDL_GetError() );
		return;
	}

	_success_ful = true;

	// Workaround for a race condition that causes SDL to segfault
	SDL_Delay(50);

	m_inputAudioHandle = m_audioHandle;
	m_inputAudioHandle.callback = sdlInputAudioCallback;

	const auto inputDevice = ConfigManager::inst()->value(SectionSDL, InputDeviceSDL).toStdString();
	const bool isDefaultInput = inputDevice.empty();

	// Try with the configured device
	const auto inputDeviceCStr = isDefaultInput ? nullptr : inputDevice.c_str();
	m_inputDevice = SDL_OpenAudioDevice(inputDeviceCStr, 1, &m_inputAudioHandle, &actual,
		SDL_AUDIO_ALLOW_CHANNELS_CHANGE);

	// If we did not get a device ID try again with the default device if we did not try that before
	if (m_inputDevice == 0 && !isDefaultInput)
	{
		m_inputDevice = SDL_OpenAudioDevice(nullptr, 1, &m_inputAudioHandle, &actual,
			SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
	}

	if (m_inputDevice != 0 && actual.format == AUDIO_F32SYS && actual.channels > 0)
	{
		m_inputChannels = actual.channels;
		setCaptureChannels(std::min<ch_cnt_t>(m_inputChannels, 2));
		m_inputBuffer.resize(std::max<std::size_t>(actual.samples, _audioEngine->framesPerAudioBuffer()));
		m_supportsCapture = true;
	}
	else
	{
		if (m_inputDevice != 0)
		{
			SDL_CloseAudioDevice(m_inputDevice);
			m_inputDevice = 0;
		}
		m_supportsCapture = false;
		qWarning("Couldn't open SDL capture device: %s\n", SDL_GetError());
	}
}




AudioSdl::~AudioSdl()
{
	if (m_inputDevice != 0)
		SDL_CloseAudioDevice(m_inputDevice);
	if (m_outputDevice != 0)
		SDL_CloseAudioDevice(m_outputDevice);

	SDL_Quit();
}




void AudioSdl::startProcessingImpl()
{
	if (m_outputDevice != 0) { SDL_PauseAudioDevice(m_outputDevice, 0); }
	if (m_inputDevice != 0) { SDL_PauseAudioDevice(m_inputDevice, 0); }
}




void AudioSdl::stopProcessingImpl()
{
	if (m_outputDevice != 0 && SDL_GetAudioDeviceStatus(m_outputDevice) == SDL_AUDIO_PLAYING)
	{
		if (m_inputDevice != 0) { SDL_LockAudioDevice(m_inputDevice); }
		SDL_LockAudioDevice(m_outputDevice);
		if (m_inputDevice != 0) { SDL_PauseAudioDevice(m_inputDevice, 1); }
		SDL_PauseAudioDevice(m_outputDevice, 1);
		SDL_UnlockAudioDevice(m_outputDevice);
		if (m_inputDevice != 0) { SDL_UnlockAudioDevice(m_inputDevice); }
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

void AudioSdl::sdlInputAudioCallback(void *_udata, Uint8 *_buf, int _len) {
	auto _this = static_cast<AudioSdl*>(_udata);

	_this->sdlInputAudioCallback( _buf, _len );
}

void AudioSdl::sdlInputAudioCallback(Uint8 *_buf, int _len) {
	if (m_inputChannels == 0 || _len <= 0) { return; }
	const auto frames = static_cast<f_cnt_t>(_len) / (sizeof(float) * m_inputChannels);
	if (frames > m_inputBuffer.size()) { return; }
	const auto samples = reinterpret_cast<const float*>(_buf);
	for (f_cnt_t frame = 0; frame < frames; ++frame)
	{
		const auto source = frame * m_inputChannels;
		m_inputBuffer[frame][0] = samples[source];
		m_inputBuffer[frame][1] = samples[source + std::min<ch_cnt_t>(1, m_inputChannels - 1)];
	}
	audioEngine()->pushInputFrames(m_inputBuffer.data(), frames);
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

	m_inputDeviceComboBox = new QComboBox(this);

	populateInputDeviceComboBox();

	form->addRow(tr("Input device"), m_inputDeviceComboBox);
}




void AudioSdl::setupWidget::saveSettings()
{
	const auto currentPlaybackDevice = m_playbackDeviceComboBox->currentText();
	if (currentPlaybackDevice == s_systemDefaultDevice)
	{
		// Represent the default playback device with an empty string
		ConfigManager::inst()->setValue(SectionSDL, PlaybackDeviceSDL, "");
	}
	else if (!currentPlaybackDevice.isEmpty())
	{
		ConfigManager::inst()->setValue(SectionSDL, PlaybackDeviceSDL, currentPlaybackDevice);
	}

	const auto currentInputDevice = m_inputDeviceComboBox->currentText();
	if (currentInputDevice == s_systemDefaultDevice)
	{
		// Represent the default input device with an empty string
		ConfigManager::inst()->setValue(SectionSDL, InputDeviceSDL, "");
	}
	else if (!currentInputDevice.isEmpty())
	{
		ConfigManager::inst()->setValue(SectionSDL, InputDeviceSDL, currentInputDevice);
	}
}

void AudioSdl::setupWidget::populatePlaybackDeviceComboBox()
{
	m_playbackDeviceComboBox->addItem(s_systemDefaultDevice);

	QStringList playbackDevices;
	const int numberOfPlaybackDevices = SDL_GetNumAudioDevices(0);
	for (int i = 0; i < numberOfPlaybackDevices; ++i)
	{
		const QString deviceName = SDL_GetAudioDeviceName(i, 0);
		playbackDevices.append(deviceName);
	}

	playbackDevices.sort();

	m_playbackDeviceComboBox->addItems(playbackDevices);

	const auto playbackDevice = ConfigManager::inst()->value(SectionSDL, PlaybackDeviceSDL);
	m_playbackDeviceComboBox->setCurrentText(playbackDevice.isEmpty() ? s_systemDefaultDevice : playbackDevice);
}

void AudioSdl::setupWidget::populateInputDeviceComboBox()
{
	m_inputDeviceComboBox->addItem(s_systemDefaultDevice);

	QStringList inputDevices;
	const int numberOfInputDevices = SDL_GetNumAudioDevices(1);
	for (int i = 0; i < numberOfInputDevices; ++i)
	{
		const QString deviceName = SDL_GetAudioDeviceName(i, 1);
		inputDevices.append(deviceName);
	}

	inputDevices.sort();

	m_inputDeviceComboBox->addItems(inputDevices);

	// Set the current device to the one in the configuration
	const auto inputDevice = ConfigManager::inst()->value(SectionSDL, InputDeviceSDL);
	m_inputDeviceComboBox->setCurrentText(inputDevice.isEmpty() ? s_systemDefaultDevice : inputDevice);
}


} // namespace mxm

#endif // MXM_HAVE_SDL
