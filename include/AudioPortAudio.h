/*
 * AudioPortAudio.h - device-class that performs PCM-output via PortAudio
 *
 * Copyright (c) 2008 Csaba Hruska <csaba.hruska/at/gmail.com>
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

#ifndef MXM_AUDIO_PORTAUDIO_H
#define MXM_AUDIO_PORTAUDIO_H

#include "mxmconfig.h"

#ifdef MXM_HAVE_PORTAUDIO

#include <QComboBox>
#include <QFormLayout>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <QTimer>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <vector>
#include <portaudio.h>

#include "AudioDevice.h"
#include "AudioDeviceSetupWidget.h"

namespace mxm {

namespace detail {
class PortAudioInitializationGuard
{
public:
	PortAudioInitializationGuard()
		: m_error(Pa_Initialize())
	{
		if (m_error != paNoError) { throw std::runtime_error{"PortAudio: could not initialize"}; }
	}

	~PortAudioInitializationGuard()
	{
		if (m_error == paNoError) { Pa_Terminate(); }
	}

	PortAudioInitializationGuard(const PortAudioInitializationGuard&) = delete;
	PortAudioInitializationGuard(PortAudioInitializationGuard&&) = delete;
	PortAudioInitializationGuard& operator=(const PortAudioInitializationGuard&) = delete;
	PortAudioInitializationGuard& operator=(PortAudioInitializationGuard&&) = delete;

private:
	PaError m_error = paNoError;
};
} // namespace detail

class AudioPortAudio : public AudioDevice
{
public:
	AudioPortAudio(bool& successful, AudioEngine* engine);
	~AudioPortAudio() override;

	AudioPortAudio(const AudioPortAudio&) = delete;
	AudioPortAudio(AudioPortAudio&&) = delete;
	AudioPortAudio& operator=(const AudioPortAudio&) = delete;
	AudioPortAudio& operator=(AudioPortAudio&&) = delete;

	static auto name() -> QString { return QT_TRANSLATE_NOOP("AudioDeviceSetupWidget", "PortAudio"); }

private:
	void startProcessingImpl() override;
	void stopProcessingImpl() override;

	auto supportsTrackInputPorts() const -> bool override { return m_supportsCapture; }
	auto registerTrackInputPort(quint64 id, const QString& name, ch_cnt_t channels) -> bool override;
	void unregisterTrackInputPort(quint64 id) override;
	auto inputSources() const -> std::vector<AudioInputSource> override;
	void setTrackInputSource(quint64 id, const QStringList& endpointPorts) override;

	static int processCallback(const void* input, void* output, unsigned long frameCount,
		const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags statusFlags, void* userData);
	static int trackInputCallback(const void* input, void* output, unsigned long frameCount,
		const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags statusFlags, void* userData);
	static void streamFinishedCallback(void* userData);
	void handleStreamFinished();
	void checkStreamStatus();

	struct TrackInput;
	struct InputDeviceCapture;
	void detachTrackInput(TrackInput& input);
	void closeInputDevice(InputDeviceCapture& capture);

	detail::PortAudioInitializationGuard m_initGuard;
	PaStream* m_paStream = nullptr;
	QString m_monitoredOutputDevice;
	std::atomic_bool m_streamFinishedReported{false};
	QTimer m_streamMonitor;
	std::unordered_map<quint64, std::unique_ptr<TrackInput>> m_trackInputs;
	std::unordered_map<PaDeviceIndex, std::unique_ptr<InputDeviceCapture>> m_inputDevices;
};
} // namespace mxm

namespace mxm::gui {
class AudioPortAudioSetupWidget : public AudioDeviceSetupWidget
{
public:
	AudioPortAudioSetupWidget(QWidget* parent);

	void show() override;
	void saveSettings() override;

private:
	class OutputSelectorWidget;
	QComboBox* m_backendComboBox = nullptr;
	OutputSelectorWidget* m_outputDevice = nullptr;
};
} // namespace mxm::gui

#endif // MXM_HAVE_PORTAUDIO

#endif // MXM_AUDIO_PORTAUDIO_H
