/*
 * AudioDevice.h - base-class for audio-devices, used by MXM audio engine
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

#ifndef MXM_AUDIO_DEVICE_H
#define MXM_AUDIO_DEVICE_H

#include <QMutex>
#include <samplerate.h>

#include "AudioChannelLayout.h"
#include "MxmTypes.h"

class QThread;

namespace mxm
{

class AudioEngine;
class AudioBusHandle;
class SampleFrame;


class AudioDevice
{
public:
	AudioDevice( const ch_cnt_t _channels, AudioEngine* audioEngine );
	virtual ~AudioDevice();

	// if audio-driver supports ports, classes inheriting AudioBusHandle
	// (e.g. channel-tracks) can register themselves for making
	// audio-driver able to collect their individual output and provide
	// them at a specific port - currently only supported by JACK
	virtual void registerPort(AudioBusHandle* port);
	virtual void unregisterPort(AudioBusHandle* port);
	virtual void renamePort(AudioBusHandle* port);

	inline bool supportsCapture() const
	{
		return m_supportsCapture;
	}

	//! True when the backend can expose independently routable track inputs.
	virtual auto supportsTrackInputPorts() const -> bool { return false; }

	//! Creates backend capture ports for one recordable track.
	virtual void registerTrackInputPort(quint64 id, const QString& name, ch_cnt_t channels)
	{
		Q_UNUSED(id); Q_UNUSED(name); Q_UNUSED(channels);
	}
	virtual void unregisterTrackInputPort(quint64 id) { Q_UNUSED(id); }
	virtual void renameTrackInputPort(quint64 id, const QString& name)
	{
		Q_UNUSED(id); Q_UNUSED(name);
	}

	inline ch_cnt_t captureChannels() const
	{
		return m_captureChannels;
	}

	//! @returns the channel layout reported by the backend, if it is known.
	inline const AudioChannelLayout& captureLayout() const
	{
		return m_captureLayout;
	}

	inline sample_rate_t sampleRate() const
	{
		return m_sampleRate;
	}

	void startProcessing();

	void stopProcessing();

	bool isRunning() const { return m_running.test(std::memory_order_acquire); }

protected:
	// convert a given audio-buffer to a buffer in signed 16-bit samples
	// returns num of bytes in outbuf
	int convertToS16(const SampleFrame* _ab, const f_cnt_t _frames, int_sample_t* _output_buffer,
		const bool _convert_endian = false);

	// clear given signed-int-16-buffer
	void clearS16Buffer(int_sample_t* _outbuf, const f_cnt_t _frames);

	ch_cnt_t channels() const { return m_channels; }

	AudioEngine* audioEngine() { return m_audioEngine; }

	void setSampleRate(const sample_rate_t _new_sr) { m_sampleRate = _new_sr; }
	void setChannels(const ch_cnt_t channels) { m_channels = channels; }
	void setCaptureChannels(const ch_cnt_t channels)
	{
		m_captureChannels = channels;
		m_captureLayout = channels <= DEFAULT_CHANNELS
			? AudioChannelLayout::standard(channels) : AudioChannelLayout::discrete(channels);
	}
	void setCaptureLayout(AudioChannelLayout layout)
	{
		m_captureChannels = layout.channels();
		m_captureLayout = std::move(layout);
	}
	//! Report backend-detected runtime route changes through the backend-independent UI signal.
	void reportDeviceChange(
		bool input, const QString& previousDevice, const QString& currentDevice, bool available);

	static void stopProcessingThread( QThread * thread );
protected:
	bool m_supportsCapture;
	ch_cnt_t m_captureChannels;
	AudioChannelLayout m_captureLayout;

private:
	virtual void startProcessingImpl() = 0;
	virtual void stopProcessingImpl() = 0;

	sample_rate_t m_sampleRate;
	ch_cnt_t m_channels;
	AudioEngine* m_audioEngine = nullptr;
	std::atomic_flag m_running = ATOMIC_FLAG_INIT;
};

} // namespace mxm

#endif // MXM_AUDIO_DEVICE_H
