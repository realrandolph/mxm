/*
 * SamplePlayHandle.cpp - implementation of class SamplePlayHandle
 *
 * Copyright (c) 2005-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "SamplePlayHandle.h"
#include "AudioEngine.h"
#include "AudioBusHandle.h"
#include "AudioChannelBuffer.h"
#include "Engine.h"
#include "PatternTrack.h"
#include "SampleClip.h"
#include "SampleTrack.h"

namespace mxm
{

SamplePlayHandle::SamplePlayHandle(Sample* sample, bool ownAudioBusHandle)
	: PlayHandle(Type::SamplePlayHandle)
	, m_sample(sample)
	, m_ownAudioBusHandle(ownAudioBusHandle)
{
	if (ownAudioBusHandle)
	{
		setAudioBusHandle(new AudioBusHandle("SamplePlayHandle", false));
	}
}




SamplePlayHandle::SamplePlayHandle( const QString& sampleFile ) :
	SamplePlayHandle(new Sample(SampleBuffer::fromFile(sampleFile)), true)
{
}




SamplePlayHandle::SamplePlayHandle( SampleClip* clip ) :
	SamplePlayHandle(&clip->sample(), false)
{
	m_track = clip->getTrack();
	setAudioBusHandle(((SampleTrack *)clip->getTrack())->audioBusHandle());
}




SamplePlayHandle::~SamplePlayHandle()
{
	if(m_ownAudioBusHandle)
	{
		delete audioBusHandle();
		delete m_sample;
	}
}




auto SamplePlayHandle::multichannelBuffer() -> AudioBuffer*
{
	return m_useMultichannel ? m_multichannelBuffer.get() : nullptr;
}




auto SamplePlayHandle::multichannelLayout() const -> AudioChannelLayout
{
	return m_useMultichannel ? m_multichannelLayout : AudioChannelLayout::stereo();
}




void SamplePlayHandle::play( SampleFrame* buffer )
{
	const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();

	AudioBusHandle* bus = audioBusHandle();
	const bool multichannelEligible = bus != nullptr
		&& bus->channels() > DEFAULT_CHANNELS
		&& m_sample != nullptr
		&& m_sample->buffer() != nullptr
		&& m_sample->buffer()->channels() > DEFAULT_CHANNELS;

	if (!multichannelEligible)
	{
		m_useMultichannel = false;

		if( framesDone() >= totalFrames() )
		{
			zeroSampleFrames(buffer, fpp);
			return;
		}

		SampleFrame* workingBuffer = buffer;
		f_cnt_t frames = fpp;

		// apply offset for the first period
		if( framesDone() == 0 )
		{
			zeroSampleFrames(buffer, offset());
			workingBuffer += offset();
			frames -= offset();
		}

		if( !( m_track && m_track->isMuted() )
					&& !(m_patternTrack && m_patternTrack->isMuted()))
		{
			// SamplePlayHandle always plays the sample at its original pitch;
			// it is used only for previews, SampleTracks and the metronome.
			if (!m_sample->play(workingBuffer, &m_state, frames))
			{
				zeroSampleFrames(workingBuffer, frames);
			}
		}

		m_frame += frames;
		return;
	}

	m_useMultichannel = true;

	if( framesDone() >= totalFrames() )
	{
		zeroSampleFrames(buffer, fpp);
		if (m_multichannelBuffer)
		{
			m_multichannelBuffer->silenceAllChannels();
		}
		return;
	}

	const auto& busLayout = bus->channelLayout();
	const auto nativeChannels = m_sample->buffer()->channels();

	if (!m_multichannelBuffer || m_multichannelLayout != busLayout
		|| m_multichannelBuffer->frames() != fpp
		|| !m_multichannelState || m_multichannelState->channels() != nativeChannels)
	{
		const int currentIndex = m_multichannelState ? m_multichannelState->frameIndex() : 0;
		m_multichannelLayout = busLayout;
		m_multichannelBuffer = createChannelBuffer(fpp, m_multichannelLayout);
		m_multichannelState = std::make_unique<Sample::PlaybackState>(
			AudioResampler::Mode::Linear, nativeChannels, currentIndex);
	}

	m_multichannelBuffer->silenceAllChannels();
	zeroSampleFrames(buffer, fpp);

	f_cnt_t frames = fpp;
	f_cnt_t frameOffset = 0;

	if (framesDone() == 0)
	{
		frameOffset = offset();
		frames -= frameOffset;
	}

	const bool isMuted = (m_track && m_track->isMuted())
		|| (m_patternTrack && m_patternTrack->isMuted());

	if (!isMuted)
	{
		auto logical = logicalBuffers(*m_multichannelBuffer, m_multichannelLayout);
		if (frameOffset > 0)
		{
			std::array<float*, MaxChannelsPerAudioBuffer> offsetPointers;
			for (ch_cnt_t ch = 0; ch < logical.channels(); ++ch)
			{
				offsetPointers[ch] = logical.bufferPtr(ch) + frameOffset;
			}
			PlanarBufferView<float> workingLogical{offsetPointers.data(), logical.channels(), frames};
			m_sample->play(workingLogical, m_multichannelLayout, m_multichannelState.get(), frames);
		}
		else
		{
			m_sample->play(logical, m_multichannelLayout, m_multichannelState.get(), frames);
		}

		m_multichannelBuffer->updateAllSilenceFlags();
	}

	m_frame += frames;
}




bool SamplePlayHandle::isFinished() const
{
	return framesDone() >= totalFrames() && m_doneMayReturnTrue == true;
}




bool SamplePlayHandle::isFromTrack( const Track * _track ) const
{
	return m_track == _track || m_patternTrack == _track;
}




f_cnt_t SamplePlayHandle::totalFrames() const
{
	return (m_sample->endFrame() - m_sample->startFrame()) *
			(static_cast<float>(Engine::audioEngine()->outputSampleRate()) / m_sample->sampleRate());
}


} // namespace mxm
