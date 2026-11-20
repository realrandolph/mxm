/*
 * AudioPortAudio.cpp - device-class that performs PCM-output via PortAudio
 *
 * Copyright (c) 2008 Csaba Hruska <csaba.hruska/at/gmail.com>
 * Copyright (c) 2010 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include <algorithm>
#include <iostream>

#include "mxmconfig.h"

#ifdef MXM_HAVE_PORTAUDIO

#include "AudioBufferView.h"
#include "AudioEngine.h"
#include "AudioPortAudio.h"
#include "ConfigManager.h"
#include "LcdSpinBox.h"

namespace {
enum class Direction
{
	Input,
	Output
};

constexpr auto tag()
{
	return "audioportaudio";
}

constexpr auto backendAttribute()
{
	return "backend";
}

constexpr auto deviceNameAttribute(Direction direction)
{
	switch (direction)
	{
	case Direction::Input:
		return "inputdevice";
	case Direction::Output:
		return "outputdevice";
	}

	return "";
}

constexpr auto channelsAttribute(Direction direction)
{
	switch (direction)
	{
	case Direction::Input:
		return "inputchannels";
	case Direction::Output:
		return "outputchannels";
	}

	return "";
}

int maxChannels(const PaDeviceInfo* info, Direction direction)
{
	switch (direction)
	{
	case Direction::Input:
		return info->maxInputChannels;
	case Direction::Output:
		return info->maxOutputChannels;
	}

	return 0;
}

} // namespace

namespace mxm {
AudioPortAudio::AudioPortAudio(bool& successful, AudioEngine* engine)
	: AudioDevice(DEFAULT_CHANNELS, engine)
{
	const auto numDevices = Pa_GetDeviceCount();
	if (numDevices < 0)
	{
		std::cerr << "Pa_GetDeviceCount() failed: " << Pa_GetErrorText(numDevices) << '\n';
		successful = false;
		return;
	}

	const auto backend = ConfigManager::inst()->value(tag(), backendAttribute());
	
	const auto inputDeviceName = ConfigManager::inst()->value(tag(), deviceNameAttribute(Direction::Input));
	auto inputDeviceChannels = ConfigManager::inst()->value(tag(), channelsAttribute(Direction::Input)).toInt();

	const auto outputDeviceName = ConfigManager::inst()->value(tag(), deviceNameAttribute(Direction::Output));
	const auto outputDeviceChannels = ConfigManager::inst()->value(tag(), channelsAttribute(Direction::Output)).toInt();

	auto inputDeviceIndex = paNoDevice;
	auto outputDeviceIndex = paNoDevice;

	auto backendIndex = -1;
	for (auto i = 0, backendCount = Pa_GetHostApiCount(); i < backendCount; ++i)
	{
		const auto* hostApiInfo = Pa_GetHostApiInfo(i);
		if (hostApiInfo && hostApiInfo->name == backend)
		{
			backendIndex = i;
			break;
		}
	}
	if (backendIndex < 0)
	{
		backendIndex = Pa_GetDefaultHostApi();
	}

	for (auto i = 0; i < numDevices && (inputDeviceIndex == paNoDevice || outputDeviceIndex == paNoDevice); ++i)
	{
		const auto deviceInfo = Pa_GetDeviceInfo(i);
		if (deviceInfo->hostApi == backendIndex)
		{
			if (deviceInfo->name == inputDeviceName) { inputDeviceIndex = i; }
			if (deviceInfo->name == outputDeviceName) { outputDeviceIndex = i; }
		}
	}

	if (inputDeviceIndex == paNoDevice && inputDeviceName.isEmpty())
	{
		const auto* hostApiInfo = backendIndex >= 0 ? Pa_GetHostApiInfo(backendIndex) : nullptr;
		const auto defaultInput = hostApiInfo && hostApiInfo->defaultInputDevice != paNoDevice
			? hostApiInfo->defaultInputDevice : Pa_GetDefaultInputDevice();
		if (defaultInput != paNoDevice)
		{
			inputDeviceIndex = defaultInput;
		}
	}

	if (outputDeviceIndex == paNoDevice && outputDeviceName.isEmpty())
	{
		const auto* hostApiInfo = backendIndex >= 0 ? Pa_GetHostApiInfo(backendIndex) : nullptr;
		const auto defaultOutput = hostApiInfo && hostApiInfo->defaultOutputDevice != paNoDevice
			? hostApiInfo->defaultOutputDevice : Pa_GetDefaultOutputDevice();
		if (defaultOutput != paNoDevice)
		{
			outputDeviceIndex = defaultOutput;
		}
	}

	const auto* outInfo = outputDeviceIndex != paNoDevice ? Pa_GetDeviceInfo(outputDeviceIndex) : nullptr;
	m_monitoredOutputDevice = outInfo && outInfo->name ? QString::fromUtf8(outInfo->name) : outputDeviceName;

	const auto* inInfo = inputDeviceIndex != paNoDevice ? Pa_GetDeviceInfo(inputDeviceIndex) : nullptr;
	m_monitoredInputDevice = inInfo && inInfo->name ? QString::fromUtf8(inInfo->name) : inputDeviceName;

	if (inputDeviceIndex != paNoDevice)
	{
		const auto maxInputChannels = Pa_GetDeviceInfo(inputDeviceIndex)->maxInputChannels;
		if (maxInputChannels > 0)
		{
			inputDeviceChannels = std::clamp(inputDeviceChannels > 0 ? inputDeviceChannels : 2,
			1, std::min(maxInputChannels, static_cast<int>(MaxChannelsPerAudioBuffer)));
		}
		else { inputDeviceIndex = paNoDevice; }
	}
	else { inputDeviceChannels = 0; }

	const auto sampleRate = engine->baseSampleRate();
	const auto framesPerBuffer = engine->framesPerPeriod();
	const auto inputLatency
		= inputDeviceIndex == paNoDevice ? 0. : Pa_GetDeviceInfo(inputDeviceIndex)->defaultLowInputLatency;
	const auto outputLatency
		= outputDeviceIndex == paNoDevice ? 0. : Pa_GetDeviceInfo(outputDeviceIndex)->defaultLowOutputLatency;

	const auto inputParameters = PaStreamParameters {
		.device = inputDeviceIndex,
		.channelCount = inputDeviceChannels,
		.sampleFormat = paFloat32,
		.suggestedLatency = inputLatency,
		.hostApiSpecificStreamInfo = nullptr
	};
	
	const auto outputParameters = PaStreamParameters {
		.device = outputDeviceIndex,
		.channelCount = outputDeviceChannels,
		.sampleFormat = paFloat32,
		.suggestedLatency = outputLatency,
		.hostApiSpecificStreamInfo = nullptr
	};

	const auto inputParametersPtr = inputDeviceIndex == paNoDevice ? nullptr : &inputParameters; 
	const auto outputParametersPtr = outputDeviceIndex == paNoDevice ? nullptr : &outputParameters;
	auto err = Pa_IsFormatSupported(inputParametersPtr, outputParametersPtr, sampleRate);

	if (err != paFormatIsSupported)
	{
		std::cerr << "Pa_IsFormatSupported() failed: " << Pa_GetErrorText(err) << '\n';
		successful = false;
		return;
	}

	err = Pa_OpenStream(&m_paStream, inputParametersPtr, outputParametersPtr, sampleRate, framesPerBuffer, paNoFlag,
		&AudioPortAudio::processCallback, this);
	
	if (err != paNoError)
	{
		std::cerr << "Pa_OpenStream() failed: " << Pa_GetErrorText(err) << '\n';
		successful = false;
		return;
	}

	Pa_SetStreamFinishedCallback(m_paStream, &AudioPortAudio::streamFinishedCallback);

	successful = true;
	setSampleRate(sampleRate);
	setChannels(outputDeviceChannels);
	m_inputChannels = inputDeviceIndex == paNoDevice ? 0 : static_cast<ch_cnt_t>(inputDeviceChannels);
	setCaptureChannels(m_inputChannels);
	if (m_inputChannels > DEFAULT_CHANNELS)
	{
		// PortAudio cannot report speaker roles, so expose the captured channels as discrete.
		setCaptureLayout(AudioChannelLayout::discrete(m_inputChannels));
	}
	m_supportsCapture = m_inputChannels > 0;

	QObject::connect(&m_streamMonitor, &QTimer::timeout, [this] { checkStreamStatus(); });
	m_streamMonitor.start(1000);
}

AudioPortAudio::~AudioPortAudio()
{
	m_streamMonitor.stop();
	stopProcessing();
	Pa_CloseStream(m_paStream);
}

void AudioPortAudio::startProcessingImpl()
{
	Pa_StartStream(m_paStream);
}

void AudioPortAudio::stopProcessingImpl()
{
	Pa_StopStream(m_paStream);
}

void AudioPortAudio::streamFinishedCallback(void* userData)
{
	static_cast<AudioPortAudio*>(userData)->handleStreamFinished();
}

void AudioPortAudio::handleStreamFinished()
{
	if (isRunning() && !m_streamFinishedReported.exchange(true, std::memory_order_acq_rel))
	{
		reportDeviceChange(false, m_monitoredOutputDevice, QString{}, false);
		if (m_supportsCapture)
		{
			reportDeviceChange(true, m_monitoredInputDevice, QString{}, false);
		}
	}
}

void AudioPortAudio::checkStreamStatus()
{
	if (isRunning() && m_paStream)
	{
		const auto active = Pa_IsStreamActive(m_paStream);
		if (active == 0 || active < 0)
		{
			handleStreamFinished();
		}
	}
}

int AudioPortAudio::processCallback(const void* input, void* output, unsigned long frameCount,
	const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* userData)
{
	const auto device = static_cast<AudioPortAudio*>(userData);
	const auto channels = device->channels();
	const auto outputBuffer = reinterpret_cast<float*>(output);

	if (!device->isRunning())
	{
		if (outputBuffer) { std::fill_n(outputBuffer, frameCount * channels, 0.f); }
		return paComplete;
	}

	if (input && device->m_inputChannels > 0)
	{
		device->audioEngine()->pushInputFrames(InterleavedBufferView<const float>{
			static_cast<const float*>(input), device->m_inputChannels, frameCount});
	}

	if (outputBuffer) { device->audioEngine()->renderNextBuffer({outputBuffer, channels, frameCount}); }
	return paContinue;
}
} // namespace mxm

namespace mxm::gui {

class AudioPortAudioSetupWidget::DeviceSelectorWidget : public QGroupBox
{
public:
	DeviceSelectorWidget(const QString& deviceLabel, Direction direction, QWidget* parent = nullptr)
		: QGroupBox{parent}
		, m_deviceComboBox{new QComboBox{this}}
		, m_channelSpinBox{new LcdSpinBox{1, this}}
		, m_direction(direction)
	{
		m_channelSpinBox->setModel(&m_channelModel);

		const auto layout = new QFormLayout{this};
		m_deviceComboBox->setMinimumHeight(m_deviceComboBox->sizeHint().height());
		layout->addRow(deviceLabel, m_deviceComboBox);
		layout->addRow(tr("Channels"), m_channelSpinBox);

		connect(m_deviceComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this,
			[this](int index) { refreshChannels(m_deviceComboBox->itemData(index).toInt()); });
	}

	void refreshFromConfig(PaHostApiIndex backendIndex)
	{
		using namespace mxm;

		m_deviceComboBox->clear();

		const auto hostApiInfo = Pa_GetHostApiInfo(backendIndex);
		const auto defaultIndex = hostApiInfo ? (m_direction == Direction::Input
			? hostApiInfo->defaultInputDevice : hostApiInfo->defaultOutputDevice) : paNoDevice;
		QString resolvedDefaultName;
		if (defaultIndex != paNoDevice && defaultIndex < Pa_GetDeviceCount())
		{
			const auto* defInfo = Pa_GetDeviceInfo(defaultIndex);
			if (defInfo && defInfo->name) { resolvedDefaultName = QString::fromUtf8(defInfo->name); }
		}
		const auto defaultText = resolvedDefaultName.isEmpty()
			? tr("[System Default]")
			: QStringLiteral("%1 [%2]").arg(tr("[System Default]"), resolvedDefaultName);
		m_deviceComboBox->addItem(defaultText, defaultIndex);

		for (auto i = 0, deviceCount = Pa_GetDeviceCount(); i < deviceCount; ++i)
		{
			const auto deviceInfo = Pa_GetDeviceInfo(i);

			if (maxChannels(deviceInfo, m_direction) > 0 && deviceInfo->hostApi == backendIndex)
			{
				m_deviceComboBox->addItem(deviceInfo->name, i);
			}
		}

		const auto selectedDeviceName = ConfigManager::inst()->value(tag(), deviceNameAttribute(m_direction));
		const auto selectedDeviceIndex = selectedDeviceName.isEmpty()
			? 0 : std::max(0, m_deviceComboBox->findText(selectedDeviceName));
		m_deviceComboBox->setCurrentIndex(selectedDeviceIndex);
	}

	void refreshChannels(PaDeviceIndex deviceIndex)
	{
		if (deviceIndex < 0 || deviceIndex >= Pa_GetDeviceCount()) { return; }
		const auto maxChannelCount = maxChannels(Pa_GetDeviceInfo(deviceIndex), m_direction);
		const auto channelCount = ConfigManager::inst()->value(tag(), channelsAttribute(m_direction)).toInt();
		m_channelModel.setRange(1, m_direction == Direction::Input
			? std::min(maxChannelCount, static_cast<int>(MaxChannelsPerAudioBuffer)) : maxChannelCount);
		m_channelModel.setValue(channelCount == 0 ? DEFAULT_CHANNELS : channelCount);
		m_channelSpinBox->setNumDigits(QString::number(maxChannelCount).length());
	}

	void saveToConfig()
	{
		const auto isDefault = m_deviceComboBox->currentIndex() == 0;
		ConfigManager::inst()->setValue(tag(), deviceNameAttribute(m_direction),
			isDefault ? QString{} : m_deviceComboBox->currentText());
		ConfigManager::inst()->setValue(tag(), channelsAttribute(m_direction), QString::number(m_channelModel.value()));
	}

private:
	QComboBox* m_deviceComboBox = nullptr;
	LcdSpinBox* m_channelSpinBox = nullptr;
	IntModel m_channelModel;
	Direction m_direction;
};

AudioPortAudioSetupWidget::AudioPortAudioSetupWidget(QWidget* parent)
	: AudioDeviceSetupWidget{AudioPortAudio::name(), parent}
	, m_backendComboBox{new QComboBox{this}}
	, m_inputDevice{new DeviceSelectorWidget{tr("Input device"), Direction::Input}}
	, m_outputDevice(new DeviceSelectorWidget{tr("Output device"), Direction::Output})
{
	constexpr auto formVerticalSpacing = 10;
	const auto form = new QFormLayout{this};
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	form->setVerticalSpacing(formVerticalSpacing);

	form->addRow(tr("Backend"), m_backendComboBox);
	form->addRow(m_outputDevice);
	form->addRow(m_inputDevice);

	connect(m_backendComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
		m_inputDevice->refreshFromConfig(m_backendComboBox->itemData(index).toInt());
		m_outputDevice->refreshFromConfig(m_backendComboBox->itemData(index).toInt());
	});

}

void AudioPortAudioSetupWidget::show()
{
	static auto s_initGuard = detail::PortAudioInitializationGuard{};

	if (m_backendComboBox->count() == 0)
	{
		for (auto i = 0, backendCount = Pa_GetHostApiCount(); i < backendCount; ++i)
		{
			m_backendComboBox->addItem(Pa_GetHostApiInfo(i)->name, i);
		}

		const auto selectedBackendName = ConfigManager::inst()->value(tag(), backendAttribute());
		const auto selectedBackendIndex = std::max(0, m_backendComboBox->findText(selectedBackendName));
		m_backendComboBox->setCurrentIndex(selectedBackendIndex);
	}

	AudioDeviceSetupWidget::show();
}

void AudioPortAudioSetupWidget::saveSettings()
{
	ConfigManager::inst()->setValue(tag(), backendAttribute(), m_backendComboBox->currentText());
	m_inputDevice->saveToConfig();
	m_outputDevice->saveToConfig();
}
} // namespace mxm::gui

#endif // MXM_HAVE_PORTAUDIO
