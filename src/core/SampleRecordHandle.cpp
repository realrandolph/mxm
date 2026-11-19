/*
 * SampleRecordHandle.cpp - implementation of class SampleRecordHandle
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */


#include "SampleRecordHandle.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <QMetaObject>

#include "AudioChannelLayout.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "SampleCapture.h"
#include "SampleClip.h"
#include "SampleTrack.h"


namespace mxm
{


SampleRecordHandle::SampleRecordHandle(SampleClip* clip, std::shared_ptr<SampleRecordSession> session) :
	PlayHandle( Type::SampleRecordPlayHandle ),
	m_track( dynamic_cast<SampleTrack*>(clip->getTrack()) ),
	m_clip( clip ),
	m_session(std::move(session)),
	m_recordChannel(0),
	m_inputMonitoring(false)
{
	if (m_track)
	{
		m_recordChannel = m_track->recordChannel();
		m_inputMonitoring = m_track->isInputMonitoring();
		if (m_inputMonitoring) { setAudioBusHandle(m_track->audioBusHandle()); }
	}
	setUsesBuffer(m_inputMonitoring);
}




SampleRecordHandle::~SampleRecordHandle()
{
	// Capture finalization belongs to SampleTrack::stopRecording(), which runs
	// on the UI thread. If no capture was ever started, remove the empty clip.
	if (!m_clip || (m_session && m_session->capture)) { return; }
	auto clip = m_clip;
	QMetaObject::invokeMethod(clip.data(), [clip] { if (clip) { delete clip.data(); } }, Qt::QueuedConnection);
}




void SampleRecordHandle::play(SampleFrame* workingBuffer)
{
	if (!m_track || !m_session->active.load(std::memory_order_relaxed)) { return; }

	const auto input = Engine::audioEngine()->inputBuffer();
	if (input.empty()) { return; }

	if (m_inputMonitoring && workingBuffer)
	{
		monitor(input, workingBuffer, std::min(input.frames(), Engine::audioEngine()->framesPerPeriod()));
	}
	writeBuffer(input);
}




void SampleRecordHandle::monitor(
	PlanarBufferView<const float> input, SampleFrame* workingBuffer, f_cnt_t outputFrames)
{
	if (!m_session->capture) { return; }

	const auto layout = m_session->capture->layout();
	const auto channels = layout.channels();
	const auto sourceChannel = channels <= 1 ? ch_cnt_t{0}
		: static_cast<ch_cnt_t>(std::clamp(m_recordChannel - 1, 0, static_cast<int>(channels) - 1));

	auto interleaved = std::array<float, MaxChannelsPerLayout>{};
	for (f_cnt_t frame = 0; frame < outputFrames; ++frame)
	{
		auto& output = workingBuffer[frame];
		switch (m_recordChannel)
		{
		case 1:
			output[0] = input.bufferPtr(sourceChannel)[frame];
			output[1] = 0.f;
			break;
		case 2:
			output[0] = 0.f;
			output[1] = input.bufferPtr(sourceChannel)[frame];
			break;
		default:
			for (ch_cnt_t channel = 0; channel < channels; ++channel)
			{
				interleaved[channel] = input.bufferPtr(channel)[frame];
			}
			downmixFrameToStereo(interleaved.data(), layout, output[0], output[1]);
			break;
		}
	}
}




bool SampleRecordHandle::isFinished() const
{
	return !m_track || !m_session->active.load(std::memory_order_relaxed);
}




bool SampleRecordHandle::isFromTrack( const Track * _track ) const
{
	return m_track == _track;
}




void SampleRecordHandle::writeBuffer( PlanarBufferView<const float> frames )
{
	if (frames.empty() || !m_session->capture) { return; }

	auto peak = 0.f;
	for (ch_cnt_t channel = 0; channel < frames.channels(); ++channel)
	{
		const auto* data = frames.bufferPtr(channel);
		for (f_cnt_t frame = 0; frame < frames.frames(); ++frame)
		{
			peak = std::max(peak, std::abs(data[frame]));
		}
	}

	m_session->capture->write(frames, m_recordChannel);
	m_session->recordedFrames.store(m_session->capture->recordedFrames(), std::memory_order_relaxed);
	if (m_track)
	{
		m_track->updateInputLevel(peak);
	}
}


} // namespace mxm
