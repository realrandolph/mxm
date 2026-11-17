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

#include "AudioEngine.h"
#include "Engine.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"


namespace mxm
{


SampleRecordHandle::SampleRecordHandle( SampleClip* clip ) :
	PlayHandle( Type::SampleRecordPlayHandle ),
	m_track( clip->getTrack() ),
	m_clip( clip ),
	m_recordInput(0),
	m_inputMonitoring(false)
{
	if (auto sampleTrack = dynamic_cast<SampleTrack*>(m_track))
	{
		m_recordInput = sampleTrack->recordInput();
		m_inputMonitoring = sampleTrack->isInputMonitoring();
		if (m_inputMonitoring) { setAudioBusHandle(sampleTrack->audioBusHandle()); }
	}
	m_recordedFrames.reserve(Engine::audioEngine()->outputSampleRate());
}




SampleRecordHandle::~SampleRecordHandle()
{
	if (!m_recordedFrames.empty())
	{
		m_clip->setSampleBuffer(std::make_shared<const SampleBuffer>(std::move(m_recordedFrames),
			Engine::audioEngine()->inputSampleRate()));
	}
}




void SampleRecordHandle::play(SampleFrame* workingBuffer)
{
	if (!Engine::getSong()->isRecording()) { return; }

	const auto recbuf = Engine::audioEngine()->inputBuffer();
	const auto frames = Engine::audioEngine()->inputBufferFrames();
	if (m_inputMonitoring && workingBuffer)
	{
		const auto outputFrames = std::min(frames, Engine::audioEngine()->framesPerPeriod());
		for (f_cnt_t frame = 0; frame < outputFrames; ++frame)
		{
			const auto& input = recbuf[frame];
			auto& output = workingBuffer[frame];
			if (m_recordInput == 1) { output[0] = output[1] = input[0]; }
			else if (m_recordInput == 2) { output[0] = output[1] = input[1]; }
			else { output = input; }
		}
	}
	writeBuffer(recbuf, frames);
}




bool SampleRecordHandle::isFinished() const
{
	return false;
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
		if (m_recordInput == 1)
		{
			recorded[0] = recorded[1] = _ab[frame][0];
		}
		else if (m_recordInput == 2)
		{
			recorded[0] = recorded[1] = _ab[frame][1];
		}
		else
		{
			recorded = _ab[frame];
		}
		peak = std::max({peak, std::abs(recorded[0]), std::abs(recorded[1])});
		m_recordedFrames.push_back(recorded);
	}
	if (auto sampleTrack = dynamic_cast<SampleTrack*>(m_track)) { sampleTrack->updateInputLevel(peak); }
}


} // namespace mxm
