/*
 * AudioFileWave.cpp - audio-device which encodes wave-stream and writes it
 *                     into a WAVE-file. This is used for song-export.
 *
 * Copyright (c) 2004-2013 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "AudioFileWave.h"
#include "endian_handling.h"
#include "AudioEngine.h"

#include <vector>


namespace mxm
{

namespace
{

auto sndfileChannelMap(SpeakerRole role) -> int
{
	switch (role)
	{
	case SpeakerRole::FrontLeft: return SF_CHANNEL_MAP_FRONT_LEFT;
	case SpeakerRole::FrontRight: return SF_CHANNEL_MAP_FRONT_RIGHT;
	case SpeakerRole::FrontCenter: return SF_CHANNEL_MAP_FRONT_CENTER;
	case SpeakerRole::Lfe: return SF_CHANNEL_MAP_LFE;
	case SpeakerRole::BackLeft: return SF_CHANNEL_MAP_REAR_LEFT;
	case SpeakerRole::BackRight: return SF_CHANNEL_MAP_REAR_RIGHT;
	case SpeakerRole::BackCenter: return SF_CHANNEL_MAP_REAR_CENTER;
	case SpeakerRole::SideLeft: return SF_CHANNEL_MAP_SIDE_LEFT;
	case SpeakerRole::SideRight: return SF_CHANNEL_MAP_SIDE_RIGHT;
	case SpeakerRole::FrontLeftOfCenter: return SF_CHANNEL_MAP_FRONT_LEFT_OF_CENTER;
	case SpeakerRole::FrontRightOfCenter: return SF_CHANNEL_MAP_FRONT_RIGHT_OF_CENTER;
	case SpeakerRole::TopFrontLeft: return SF_CHANNEL_MAP_TOP_FRONT_LEFT;
	case SpeakerRole::TopFrontRight: return SF_CHANNEL_MAP_TOP_FRONT_RIGHT;
	case SpeakerRole::TopBackLeft: return SF_CHANNEL_MAP_TOP_REAR_LEFT;
	case SpeakerRole::TopBackRight: return SF_CHANNEL_MAP_TOP_REAR_RIGHT;
	case SpeakerRole::Unknown: return SF_CHANNEL_MAP_INVALID;
	}
	return SF_CHANNEL_MAP_INVALID;
}

} // namespace

AudioFileWave::AudioFileWave( OutputSettings const & outputSettings,
				const AudioChannelLayout& layout, bool & successful,
				const QString & file,
				AudioEngine* audioEngine ) :
	AudioFileDevice( outputSettings, layout, file, audioEngine ),
	m_sf( nullptr )
{
	successful = outputFileOpened() && startEncoding();
}




AudioFileWave::~AudioFileWave()
{
	finishEncoding();
}




bool AudioFileWave::startEncoding()
{
	m_si.samplerate = sampleRate();
	m_si.channels = channels();
	m_si.frames = audioEngine()->framesPerPeriod();
	m_si.sections = 1;
	m_si.seekable = 0;

	// WAVE_FORMAT_EXTENSIBLE carries the speaker mask for multichannel files.
	m_si.format = channels() > DEFAULT_CHANNELS ? SF_FORMAT_WAVEX : SF_FORMAT_WAV;

	switch( getOutputSettings().getBitDepth() )
	{
	case OutputSettings::BitDepth::Depth32Bit:
		m_si.format |= SF_FORMAT_FLOAT;
		break;
	case OutputSettings::BitDepth::Depth24Bit:
		m_si.format |= SF_FORMAT_PCM_24;
		break;
	case OutputSettings::BitDepth::Depth16Bit:
	default:
		m_si.format |= SF_FORMAT_PCM_16;
		break;
	}

	// Use file handle to handle unicode file name on Windows
	m_sf = sf_open_fd( outputFileHandle(), SFM_WRITE, &m_si, false );

	if (!m_sf)
	{
		qWarning("Error: AudioFileWave::startEncoding: %s", sf_strerror(nullptr));
		return false;
	}

	// Prevent fold overs when encountering clipped data
	sf_command(m_sf, SFC_SET_CLIPPING, nullptr, SF_TRUE);

	sf_set_string ( m_sf, SF_STR_SOFTWARE, "MXM" );

	if (channels() > DEFAULT_CHANNELS && channelLayout().hasKnownRoles())
	{
		auto channelMap = std::vector<int>(channels());
		for (ch_cnt_t channel = 0; channel < channels(); ++channel)
		{
			channelMap[channel] = sndfileChannelMap(channelLayout().role(channel));
		}
		sf_command(m_sf, SFC_SET_CHANNEL_MAP_INFO, channelMap.data(),
			static_cast<int>(channelMap.size() * sizeof(int)));
	}

	return true;
}

void AudioFileWave::writeBufferImpl(PlanarBufferView<const float> buffer)
{
	OutputSettings::BitDepth bitDepth = getOutputSettings().getBitDepth();
	const auto frames = buffer.frames();
	const auto chans = channels();

	if( bitDepth == OutputSettings::BitDepth::Depth32Bit || bitDepth == OutputSettings::BitDepth::Depth24Bit )
	{
		auto interleaved = std::vector<float>(frames * chans);
		for( f_cnt_t frame = 0; frame < frames; ++frame )
		{
			for( ch_cnt_t channel = 0; channel < chans; ++channel )
			{
				interleaved[frame * chans + channel] = buffer.bufferPtr(channel)[frame];
			}
		}
		sf_writef_float( m_sf, interleaved.data(), frames );
	}
	else
	{
		auto interleaved = std::vector<int_sample_t>(frames * chans);
		convertToS16(buffer, interleaved.data(), isBigEndian());

		sf_writef_short( m_sf, interleaved.data(), frames );
	}
}




void AudioFileWave::finishEncoding()
{
	if( m_sf )
	{
		sf_close( m_sf );
	}
}

} // namespace mxm