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
#include <cstring>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <vector>

#include "mxmconfig.h"

#ifdef MXM_HAVE_PORTAUDIO

#include <QSignalBlocker>

#include "AudioBufferView.h"
#include "AudioEngine.h"
#include "AudioPortAudio.h"
#include "ConfigManager.h"
#include "LcdSpinBox.h"

namespace {
constexpr auto tag()
{
	return "audioportaudio";
}

constexpr auto backendAttribute()
{
	return "backend";
}

constexpr auto outputDeviceAttribute()
{
	return "outputdevice";
}

constexpr auto outputChannelsAttribute()
{
	return "outputchannels";
}

auto hostApiName(PaHostApiIndex index) -> QString
{
	const auto* info = index >= 0 ? Pa_GetHostApiInfo(index) : nullptr;
	return info && info->name ? QString::fromUtf8(info->name)
		: QStringLiteral("Host API %1").arg(index);
}

auto deviceName(const PaDeviceInfo* info) -> QString
{
	return info && info->name ? QString::fromUtf8(info->name) : QString{};
}

//! Distinguishes otherwise identical devices that a host API exposes twice.
auto duplicateIndex(PaDeviceIndex index, const PaDeviceInfo* info) -> int
{
	auto duplicates = 0;
	for (PaDeviceIndex other = 0; other < index; ++other)
	{
		const auto* otherInfo = Pa_GetDeviceInfo(other);
		if (otherInfo && otherInfo->hostApi == info->hostApi
			&& otherInfo->name && info->name
			&& std::strcmp(otherInfo->name, info->name) == 0)
		{
			++duplicates;
		}
	}
	return duplicates;
}

//! Stable identity independent of PortAudio's device enumeration order.
auto inputSourceId(PaDeviceIndex index) -> QString
{
	const auto* info = Pa_GetDeviceInfo(index);
	if (!info) { return {}; }

	auto id = QStringLiteral("portaudio:%1:%2").arg(
		hostApiName(info->hostApi), deviceName(info));
	const auto duplicate = duplicateIndex(index, info);
	if (duplicate > 0) { id += QStringLiteral("#%1").arg(duplicate + 1); }
	return id;
}

auto inputSourceLabel(PaDeviceIndex index) -> QString
{
	const auto* info = Pa_GetDeviceInfo(index);
	if (!info) { return {}; }

	auto label = QStringLiteral("%1 (%2)").arg(
		deviceName(info), hostApiName(info->hostApi));
	const auto duplicate = duplicateIndex(index, info);
	if (duplicate > 0) { label += QStringLiteral(" #%1").arg(duplicate + 1); }
	return label;
}

auto findInputDevice(const QString& id) -> PaDeviceIndex
{
	const auto deviceCount = Pa_GetDeviceCount();
	for (PaDeviceIndex index = 0; index < deviceCount; ++index)
	{
		const auto* info = Pa_GetDeviceInfo(index);
		if (info && info->maxInputChannels > 0 && inputSourceId(index) == id)
		{
			return index;
		}
	}
	return paNoDevice;
}

constexpr auto MaxTrackInputChannels = mxm::ch_cnt_t{8};

} // namespace

namespace mxm {

struct AudioPortAudio::TrackInput
{
	AudioEngine* engine = nullptr;
	quint64 trackId = 0;
	PaDeviceIndex device = paNoDevice;
	std::vector<ch_cnt_t> sourceChannels;
	std::vector<float> mappedFrames;
};

struct AudioPortAudio::InputDeviceCapture
{
	PaDeviceIndex device = paNoDevice;
	PaStream* stream = nullptr;
	ch_cnt_t channels = 0;
	std::vector<TrackInput*> subscribers;
};

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
	const auto outputDeviceName = ConfigManager::inst()->value(tag(), outputDeviceAttribute());
	const auto configuredOutputChannels = ConfigManager::inst()->value(tag(), outputChannelsAttribute()).toInt();

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

	auto outputDeviceIndex = paNoDevice;
	for (auto i = 0; i < numDevices; ++i)
	{
		const auto* deviceInfo = Pa_GetDeviceInfo(i);
		if (deviceInfo && deviceInfo->hostApi == backendIndex
			&& deviceInfo->name == outputDeviceName)
		{
			outputDeviceIndex = i;
			break;
		}
	}

	if (outputDeviceIndex == paNoDevice)
	{
		const auto* hostApiInfo = backendIndex >= 0 ? Pa_GetHostApiInfo(backendIndex) : nullptr;
		if (hostApiInfo) { outputDeviceIndex = hostApiInfo->defaultOutputDevice; }
	}

	const auto* outInfo = outputDeviceIndex != paNoDevice ? Pa_GetDeviceInfo(outputDeviceIndex) : nullptr;
	if (!outInfo || outInfo->maxOutputChannels <= 0)
	{
		std::cerr << "PortAudio: no usable output device\n";
		successful = false;
		return;
	}
	m_monitoredOutputDevice = deviceName(outInfo);

	const auto outputChannels = std::clamp(
		configuredOutputChannels > 0 ? configuredOutputChannels : static_cast<int>(DEFAULT_CHANNELS),
		1, outInfo->maxOutputChannels);

	const auto sampleRate = engine->baseSampleRate();
	const auto framesPerBuffer = engine->framesPerPeriod();
	const auto outputParameters = PaStreamParameters {
		.device = outputDeviceIndex,
		.channelCount = outputChannels,
		.sampleFormat = paFloat32,
		.suggestedLatency = outInfo->defaultLowOutputLatency,
		.hostApiSpecificStreamInfo = nullptr
	};

	auto err = Pa_IsFormatSupported(nullptr, &outputParameters, sampleRate);
	if (err != paFormatIsSupported)
	{
		std::cerr << "Pa_IsFormatSupported() failed: " << Pa_GetErrorText(err) << '\n';
		successful = false;
		return;
	}

	err = Pa_OpenStream(&m_paStream, nullptr, &outputParameters, sampleRate, framesPerBuffer,
		paNoFlag, &AudioPortAudio::processCallback, this);
	if (err != paNoError)
	{
		std::cerr << "Pa_OpenStream() failed: " << Pa_GetErrorText(err) << '\n';
		successful = false;
		return;
	}

	Pa_SetStreamFinishedCallback(m_paStream, &AudioPortAudio::streamFinishedCallback);

	successful = true;
	setSampleRate(sampleRate);
	setChannels(static_cast<ch_cnt_t>(outputChannels));
	m_supportsCapture = !inputSources().empty();

	QObject::connect(&m_streamMonitor, &QTimer::timeout, [this] { checkStreamStatus(); });
	m_streamMonitor.start(1000);
}

AudioPortAudio::~AudioPortAudio()
{
	m_streamMonitor.stop();
	stopProcessing();
	for (auto& [device, capture] : m_inputDevices)
	{
		Q_UNUSED(device);
		closeInputDevice(*capture);
	}
	m_inputDevices.clear();
	m_trackInputs.clear();
	if (m_paStream) { Pa_CloseStream(m_paStream); }
}

void AudioPortAudio::startProcessingImpl()
{
	if (m_paStream) { Pa_StartStream(m_paStream); }
	for (auto& [device, capture] : m_inputDevices)
	{
		Q_UNUSED(device);
		if (capture->stream) { Pa_StartStream(capture->stream); }
	}
}

void AudioPortAudio::stopProcessingImpl()
{
	if (m_paStream) { Pa_StopStream(m_paStream); }
	for (auto& [device, capture] : m_inputDevices)
	{
		Q_UNUSED(device);
		if (capture->stream) { Pa_StopStream(capture->stream); }
	}
}

void AudioPortAudio::closeInputDevice(InputDeviceCapture& capture)
{
	if (capture.stream)
	{
		Pa_StopStream(capture.stream);
		Pa_CloseStream(capture.stream);
		capture.stream = nullptr;
	}
	capture.channels = 0;
	capture.subscribers.clear();
}

void AudioPortAudio::detachTrackInput(TrackInput& input)
{
	if (input.device == paNoDevice) { return; }
	const auto found = m_inputDevices.find(input.device);
	if (found == m_inputDevices.end())
	{
		input.device = paNoDevice;
		return;
	}
	auto& capture = *found->second;
	if (capture.stream) { Pa_StopStream(capture.stream); }
	std::erase(capture.subscribers, &input);
	input.device = paNoDevice;
	if (capture.subscribers.empty())
	{
		closeInputDevice(capture);
		m_inputDevices.erase(found);
	}
	else if (isRunning() && capture.stream)
	{
		Pa_StartStream(capture.stream);
	}
}

auto AudioPortAudio::inputSources() const -> std::vector<AudioInputSource>
{
	auto sources = std::vector<AudioInputSource>{};
	const auto defaultInput = Pa_GetDefaultInputDevice();
	if (defaultInput != paNoDevice)
	{
		const auto* info = Pa_GetDeviceInfo(defaultInput);
		if (info && info->maxInputChannels > 0)
		{
			const auto channels = static_cast<ch_cnt_t>(
				std::min(info->maxInputChannels, 2));
			sources.push_back(makeAudioInputSource(
				QString::fromLatin1(SystemDefaultInputSourceId), QObject::tr("System Default"), channels,
				{QString::fromLatin1(SystemDefaultInputSourceId)}));
			const auto available = std::min(info->maxInputChannels, static_cast<int>(MaxTrackInputChannels));
			for (auto channel = 0; channel < available; ++channel)
			{
				const auto endpoint = QString::number(channel + 1);
				sources.push_back(makeAudioInputSource(
					QStringLiteral("portaudio:system:mono:%1").arg(channel + 1),
					QObject::tr("System Default — Input %1").arg(channel + 1), 1,
					{QString::fromLatin1(SystemDefaultInputSourceId), endpoint}));
				for (auto second = channel + 1; second < available; ++second)
				{
					sources.push_back(makeAudioInputSource(
						QStringLiteral("portaudio:system:pair:%1:%2").arg(channel + 1).arg(second + 1),
						QObject::tr("System Default — Inputs %1 / %2").arg(channel + 1).arg(second + 1), 2,
						{QString::fromLatin1(SystemDefaultInputSourceId), QString::number(channel + 1),
							QString::number(second + 1)}));
				}
			}
		}
	}
	const auto deviceCount = Pa_GetDeviceCount();
	for (PaDeviceIndex index = 0; index < deviceCount; ++index)
	{
		const auto* info = Pa_GetDeviceInfo(index);
		if (!info || info->maxInputChannels <= 0) { continue; }

		const auto channels = static_cast<ch_cnt_t>(std::min(info->maxInputChannels, 2));
		const auto id = inputSourceId(index);
		const auto label = inputSourceLabel(index);
		sources.push_back(makeAudioInputSource(
			id,
			label,
			channels,
			{id}));
		const auto available = std::min(info->maxInputChannels, static_cast<int>(MaxTrackInputChannels));
		for (auto channel = 0; channel < available; ++channel)
		{
			sources.push_back(makeAudioInputSource(
				id + QStringLiteral(":mono:%1").arg(channel + 1),
				QStringLiteral("%1 — Input %2").arg(label).arg(channel + 1), 1,
				{id, QString::number(channel + 1)}));
			for (auto second = channel + 1; second < available; ++second)
			{
				sources.push_back(makeAudioInputSource(
					id + QStringLiteral(":pair:%1:%2").arg(channel + 1).arg(second + 1),
					QStringLiteral("%1 — Inputs %2 / %3").arg(label).arg(channel + 1).arg(second + 1), 2,
					{id, QString::number(channel + 1), QString::number(second + 1)}));
			}
		}
	}
	return sources;
}

auto AudioPortAudio::registerTrackInputPort(quint64 id, const QString&, ch_cnt_t channels) -> bool
{
	if (id == 0 || channels != 2) { return false; }
	unregisterTrackInputPort(id);

	auto input = std::make_unique<TrackInput>();
	input->engine = audioEngine();
	input->trackId = id;
	return m_trackInputs.emplace(id, std::move(input)).second;
}

void AudioPortAudio::unregisterTrackInputPort(quint64 id)
{
	const auto found = m_trackInputs.find(id);
	if (found == m_trackInputs.end()) { return; }
	detachTrackInput(*found->second);
	m_trackInputs.erase(found);
}

void AudioPortAudio::setTrackInputSource(quint64 id, const QStringList& endpointPorts)
{
	const auto found = m_trackInputs.find(id);
	if (found == m_trackInputs.end()) { return; }
	auto& input = *found->second;
	detachTrackInput(input);
	if (endpointPorts.isEmpty())
	{
		audioEngine()->setTrackInputCaptureFailed(id, false);
		return;
	}

	const auto deviceIndex = endpointPorts.front() == QLatin1String(SystemDefaultInputSourceId)
		? Pa_GetDefaultInputDevice() : findInputDevice(endpointPorts.front());
	const auto* info = deviceIndex != paNoDevice ? Pa_GetDeviceInfo(deviceIndex) : nullptr;
	if (!info || info->maxInputChannels <= 0)
	{
		std::cerr << "PortAudio: input source not available: "
			<< endpointPorts.front().toStdString() << '\n';
		audioEngine()->setTrackInputCaptureFailed(id, true);
		return;
	}

	const auto captureChannels = static_cast<ch_cnt_t>(
		std::min(info->maxInputChannels, static_cast<int>(MaxTrackInputChannels)));
	if (captureChannels == 0)
	{
		audioEngine()->setTrackInputCaptureFailed(id, true);
		return;
	}
	input.sourceChannels.clear();
	for (qsizetype i = 1; i < endpointPorts.size(); ++i)
	{
		bool ok = false;
		const auto channel = static_cast<ch_cnt_t>(endpointPorts[i].toUInt(&ok));
		if (!ok || channel == 0 || channel > captureChannels)
		{
			audioEngine()->setTrackInputCaptureFailed(id, true);
			return;
		}
		input.sourceChannels.push_back(channel - 1);
	}
	if (input.sourceChannels.empty())
	{
		input.sourceChannels.push_back(0);
		if (captureChannels > 1) { input.sourceChannels.push_back(1); }
	}
	input.mappedFrames.resize(static_cast<std::size_t>(audioEngine()->framesPerAudioBuffer()) * 2);
	auto captureIt = m_inputDevices.find(deviceIndex);
	if (captureIt == m_inputDevices.end())
	{
		auto capture = std::make_unique<InputDeviceCapture>();
		capture->device = deviceIndex;
		capture->channels = captureChannels;
		const auto inputParameters = PaStreamParameters {
			.device = deviceIndex,
			.channelCount = captureChannels,
			.sampleFormat = paFloat32,
			.suggestedLatency = info->defaultLowInputLatency,
			.hostApiSpecificStreamInfo = nullptr
		};

		auto err = Pa_IsFormatSupported(&inputParameters, nullptr, sampleRate());
		if (err != paFormatIsSupported)
		{
			std::cerr << "PortAudio: input format not supported for "
				<< endpointPorts.front().toStdString() << ": " << Pa_GetErrorText(err) << '\n';
			audioEngine()->setTrackInputCaptureFailed(id, true);
			return;
		}

		err = Pa_OpenStream(&capture->stream, &inputParameters, nullptr, sampleRate(),
			audioEngine()->framesPerPeriod(), paNoFlag, &AudioPortAudio::trackInputCallback, capture.get());
		if (err != paNoError)
		{
			std::cerr << "PortAudio: could not open input "
				<< endpointPorts.front().toStdString() << ": " << Pa_GetErrorText(err) << '\n';
			audioEngine()->setTrackInputCaptureFailed(id, true);
			return;
		}
		captureIt = m_inputDevices.emplace(deviceIndex, std::move(capture)).first;
	}
	else if (captureIt->second->channels != captureChannels)
	{
		audioEngine()->setTrackInputCaptureFailed(id, true);
		return;
	}

	input.device = deviceIndex;
	auto& sharedCapture = *captureIt->second;
	const auto wasActive = Pa_IsStreamActive(sharedCapture.stream) == 1;
	if (wasActive) { Pa_StopStream(sharedCapture.stream); }
	sharedCapture.subscribers.push_back(&input);
	audioEngine()->setTrackInputCaptureFailed(id, false);
	if (isRunning() && Pa_IsStreamActive(sharedCapture.stream) != 1)
	{
		const auto err = Pa_StartStream(sharedCapture.stream);
		if (err != paNoError)
		{
			std::cerr << "PortAudio: could not start input "
				<< endpointPorts.front().toStdString() << ": " << Pa_GetErrorText(err) << '\n';
			detachTrackInput(input);
			audioEngine()->setTrackInputCaptureFailed(id, true);
		}
	}
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

int AudioPortAudio::processCallback(const void*, void* output, unsigned long frameCount,
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

	if (outputBuffer) { device->audioEngine()->renderNextBuffer({outputBuffer, channels, frameCount}); }
	return paContinue;
}

int AudioPortAudio::trackInputCallback(const void* input, void*, unsigned long frameCount,
	const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* userData)
{
	auto& capture = *static_cast<InputDeviceCapture*>(userData);
	if (!input || capture.channels == 0)
	{
		return paContinue;
	}

	const auto* source = static_cast<const float*>(input);
	for (auto* inputState : capture.subscribers)
	{
		if (!inputState || inputState->sourceChannels.empty()) { continue; }
		const auto scratchFrames = static_cast<f_cnt_t>(inputState->mappedFrames.size() / 2);
		for (unsigned long offset = 0; offset < frameCount;)
		{
			const auto frames = static_cast<f_cnt_t>(std::min<unsigned long>(
				frameCount - offset, scratchFrames));
			for (f_cnt_t frame = 0; frame < frames; ++frame)
			{
				const auto* sourceFrame = source + (offset + frame) * capture.channels;
				const auto scratchIndex = static_cast<std::size_t>(frame) * 2;
				inputState->mappedFrames[scratchIndex] = sourceFrame[inputState->sourceChannels[0]];
				inputState->mappedFrames[scratchIndex + 1] = inputState->sourceChannels.size() > 1
					? sourceFrame[inputState->sourceChannels[1]] : 0.f;
			}
			inputState->engine->pushTrackInputFrames(inputState->trackId,
				InterleavedBufferView<const float>{inputState->mappedFrames.data(), 2, frames});
			offset += frames;
		}
	}
	return paContinue;
}
} // namespace mxm

namespace mxm::gui {

class AudioPortAudioSetupWidget::OutputSelectorWidget : public QGroupBox
{
public:
	explicit OutputSelectorWidget(QWidget* parent = nullptr)
		: QGroupBox{parent}
		, m_deviceComboBox{new QComboBox{this}}
		, m_channelSpinBox{new LcdSpinBox{1, this}}
	{
		m_channelSpinBox->setModel(&m_channelModel);

		const auto layout = new QFormLayout{this};
		m_deviceComboBox->setMinimumHeight(m_deviceComboBox->sizeHint().height());
		layout->addRow(tr("Output device"), m_deviceComboBox);
		layout->addRow(tr("Channels"), m_channelSpinBox);

		connect(m_deviceComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this,
			[this](int index) { refreshChannels(m_deviceComboBox->itemData(index).toInt()); });
	}

	void refreshFromConfig(PaHostApiIndex backendIndex)
	{
		m_deviceComboBox->clear();

		const auto* hostApiInfo = backendIndex >= 0 ? Pa_GetHostApiInfo(backendIndex) : nullptr;
		const auto defaultIndex = hostApiInfo ? hostApiInfo->defaultOutputDevice : paNoDevice;
		QString resolvedDefaultName;
		if (defaultIndex != paNoDevice && defaultIndex < Pa_GetDeviceCount())
		{
			const auto* defInfo = Pa_GetDeviceInfo(defaultIndex);
			if (defInfo) { resolvedDefaultName = deviceName(defInfo); }
		}
		const auto defaultText = resolvedDefaultName.isEmpty()
			? tr("[System Default]")
			: QStringLiteral("%1 [%2]").arg(tr("[System Default]"), resolvedDefaultName);
		m_deviceComboBox->addItem(defaultText, defaultIndex);

		for (auto i = 0, deviceCount = Pa_GetDeviceCount(); i < deviceCount; ++i)
		{
			const auto* deviceInfo = Pa_GetDeviceInfo(i);
			if (deviceInfo && deviceInfo->maxOutputChannels > 0 && deviceInfo->hostApi == backendIndex)
			{
				m_deviceComboBox->addItem(deviceName(deviceInfo), i);
			}
		}

		const auto selectedDeviceName = ConfigManager::inst()->value(tag(), outputDeviceAttribute());
		const auto selectedDeviceIndex = selectedDeviceName.isEmpty()
			? 0 : std::max(0, m_deviceComboBox->findText(selectedDeviceName));
		m_deviceComboBox->setCurrentIndex(selectedDeviceIndex);
		refreshChannels(m_deviceComboBox->currentData().toInt());
	}

	void refreshChannels(PaDeviceIndex deviceIndex)
	{
		if (deviceIndex < 0 || deviceIndex >= Pa_GetDeviceCount()) { return; }
		const auto* deviceInfo = Pa_GetDeviceInfo(deviceIndex);
		if (!deviceInfo) { return; }
		const auto maxChannelCount = deviceInfo->maxOutputChannels;
		const auto channelCount = ConfigManager::inst()->value(tag(), outputChannelsAttribute()).toInt();
		m_channelModel.setRange(1, maxChannelCount);
		m_channelModel.setValue(channelCount == 0 ? DEFAULT_CHANNELS : channelCount);
		m_channelSpinBox->setNumDigits(QString::number(maxChannelCount).length());
	}

	void saveToConfig()
	{
		const auto isDefault = m_deviceComboBox->currentIndex() == 0;
		ConfigManager::inst()->setValue(tag(), outputDeviceAttribute(),
			isDefault ? QString{} : m_deviceComboBox->currentText());
		ConfigManager::inst()->setValue(tag(), outputChannelsAttribute(), QString::number(m_channelModel.value()));
	}

private:
	QComboBox* m_deviceComboBox = nullptr;
	LcdSpinBox* m_channelSpinBox = nullptr;
	IntModel m_channelModel;
};

AudioPortAudioSetupWidget::AudioPortAudioSetupWidget(QWidget* parent)
	: AudioDeviceSetupWidget{AudioPortAudio::name(), parent}
	, m_backendComboBox{new QComboBox{this}}
	, m_outputDevice{new OutputSelectorWidget{this}}
{
	constexpr auto formVerticalSpacing = 10;
	const auto form = new QFormLayout{this};
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	form->setVerticalSpacing(formVerticalSpacing);

	form->addRow(tr("Backend"), m_backendComboBox);
	form->addRow(m_outputDevice);

	connect(m_backendComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
		m_outputDevice->refreshFromConfig(m_backendComboBox->itemData(index).toInt());
	});
}

void AudioPortAudioSetupWidget::show()
{
	static auto s_initGuard = detail::PortAudioInitializationGuard{};

	if (m_backendComboBox->count() == 0)
	{
		const auto blocker = QSignalBlocker{m_backendComboBox};
		for (auto i = 0, backendCount = Pa_GetHostApiCount(); i < backendCount; ++i)
		{
			const auto* hostApiInfo = Pa_GetHostApiInfo(i);
			if (hostApiInfo) { m_backendComboBox->addItem(hostApiInfo->name, i); }
		}

		const auto selectedBackendName = ConfigManager::inst()->value(tag(), backendAttribute());
		const auto selectedBackendIndex = std::max(0, m_backendComboBox->findText(selectedBackendName));
		m_backendComboBox->setCurrentIndex(selectedBackendIndex);
		m_outputDevice->refreshFromConfig(m_backendComboBox->currentData().toInt());
	}

	AudioDeviceSetupWidget::show();
}

void AudioPortAudioSetupWidget::saveSettings()
{
	ConfigManager::inst()->setValue(tag(), backendAttribute(), m_backendComboBox->currentText());
	m_outputDevice->saveToConfig();
}
} // namespace mxm::gui

#endif // MXM_HAVE_PORTAUDIO
