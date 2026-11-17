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
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */


#include "SampleRecordHandle.h"

#include <algorithm>
#include <cmath>

#include <QMetaObject>

#include "AudioEngine.h"
#include "Engine.h"
#include "SampleBuffer.h"
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
	m_recordedFrames.reserve(Engine::audioEngine()->outputSampleRate());
}




SampleRecordHandle::~SampleRecordHandle()
{
	if (!m_clip) { return; }
	auto clip = m_clip;
	if (m_recordedFrames.empty())
	{
		QMetaObject::invokeMethod(clip.data(), [clip] { if (clip) { delete clip.data(); } }, Qt::QueuedConnection);
		return;
	}
	auto sampleBuffer = std::make_shared<const SampleBuffer>(std::move(m_recordedFrames),
		Engine::audioEngine()->inputSampleRate());
	QMetaObject::invokeMethod(clip.data(), [clip, sampleBuffer = std::move(sampleBuffer)]() mutable
	{
		if (clip) { clip->setSampleBuffer(std::move(sampleBuffer)); }
	}, Qt::QueuedConnection);
}




void SampleRecordHandle::play(SampleFrame* workingBuffer)
{
	if (!m_track || !m_session->active.load(std::memory_order_relaxed)) { return; }

	const auto recbuf = Engine::audioEngine()->inputBuffer();
	const auto frames = Engine::audioEngine()->inputBufferFrames();
	if (m_inputMonitoring && workingBuffer)
	{
		const auto outputFrames = std::min(frames, Engine::audioEngine()->framesPerPeriod());
		for (f_cnt_t frame = 0; frame < outputFrames; ++frame)
		{
			const auto& input = recbuf[frame];
			auto& output = workingBuffer[frame];
			if (m_recordChannel == 1) { output[0] = input[0]; output[1] = 0.f; }
			else if (m_recordChannel == 2) { output[0] = 0.f; output[1] = input[1]; }
			else { output = input; }
		}
	}
	writeBuffer(recbuf, frames);
}




bool SampleRecordHandle::isFinished() const
{
	return !m_track || !m_session->active.load(std::memory_order_relaxed);
}




bool SampleRecordHandle::isFromTrack( const Track * _track ) const
{
	return m_track == _track;
}




void SampleRecordHandle::writeBuffer( const SampleFrame* _ab, const f_cnt_t _frames )
{
	if (_frames == 0) { return; }
	float peak = 0.f;
	for( f_cnt_t frame = 0; frame < _frames; ++frame )
	{
		SampleFrame recorded;
		if (m_recordChannel == 1)
		{
			recorded[0] = _ab[frame][0];
			recorded[1] = 0.f;
		}
		else if (m_recordChannel == 2)
		{
			recorded[0] = 0.f;
			recorded[1] = _ab[frame][1];
		}
		else
		{
			recorded = _ab[frame];
		}
		peak = std::max({peak, std::abs(recorded[0]), std::abs(recorded[1])});
		m_recordedFrames.push_back(recorded);
	}
	if (m_track)
	{
		m_session->recordedFrames.fetch_add(_frames, std::memory_order_relaxed);
		m_track->updateInputLevel(peak);
	}
}


} // namespace mxm
