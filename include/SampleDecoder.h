/*
 * SampleDecoder.h - Decodes audio files in various formats
 *
 * Copyright (c) 2023 saker <sakertooth@gmail.com>
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

#ifndef MXM_SAMPLE_DECODER_H
#define MXM_SAMPLE_DECODER_H

#include <QString>
#include <array>
#include <optional>
#include <string>
#include <vector>

#include "AudioChannelLayout.h"

namespace mxm {
class SampleDecoder
{
public:
	struct Result
	{
		//! Interleaved samples, `frames * channels` elements
		std::vector<float> samples;
		ch_cnt_t channels;
		AudioChannelLayout layout;
		int sampleRate;
	};

	struct AudioType
	{
		std::string name;
		std::string extension;
	};

	struct VorbisChannelMap
	{
		AudioChannelLayout layout;
		//! Target channel index -> interleaved Vorbis channel index, for up to 8 channels.
		std::array<ch_cnt_t, 8> order{};
	};

	/**
	 * @brief Describes the Vorbis I output channel order and the equivalent
	 *        MXM channel layout in WAV/SMPTE order.
	 *
	 * Vorbis orders multichannel streams differently from WAV. The Ogg decoder
	 * remaps samples with @ref VorbisChannelMap::order so stored clips and
	 * exports use MXM's canonical channel order.
	 */
	static auto vorbisChannelMap(ch_cnt_t channels) -> VorbisChannelMap;

	static auto decode(const QString& audioFile) -> std::optional<Result>;
	static auto supportedAudioTypes() -> const std::vector<AudioType>&;
};
} // namespace mxm

#endif // MXM_SAMPLE_DECODER_H
