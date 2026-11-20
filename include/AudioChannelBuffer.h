/*
 * AudioChannelBuffer.h - allocation helpers for channel-layout-aware buffers
 *
 * Copyright (c) 2026 MXM contributors
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

#ifndef MXM_AUDIO_CHANNEL_BUFFER_H
#define MXM_AUDIO_CHANNEL_BUFFER_H

#include <algorithm>
#include <memory>

#include "AudioBuffer.h"
#include "AudioChannelLayout.h"

namespace mxm
{

/**
 * @brief Creates a buffer for a channel layout.
 *
 * Effects and play handles process stereo pairs, so every buffer has a first
 * group of exactly two channels (or the logical channel count when it is
 * larger). Layouts with one channel reserve a silent second channel; logical
 * views expose only @ref AudioChannelLayout::channels() channels.
 *
 * The returned buffer owns an interleaved stereo scratch buffer because
 * stereo-only effects and plugins still use it.
 */
inline auto createChannelBuffer(f_cnt_t frames, const AudioChannelLayout& layout)
	-> std::unique_ptr<AudioBuffer>
{
	using Groups = group_cnt_t;

	const auto logicalChannels = std::max<ch_cnt_t>(layout.channels(), 1);
	const auto physicalChannels = std::max<ch_cnt_t>(logicalChannels, DEFAULT_CHANNELS);
	const auto groups = static_cast<Groups>(physicalChannels > DEFAULT_CHANNELS ? 2 : 1);

	auto buffer = std::make_unique<AudioBuffer>(frames, physicalChannels, groups,
		std::pmr::get_default_resource(),
		[physicalChannels](group_cnt_t index, AudioBuffer::ChannelGroup&) -> ch_cnt_t {
			return index == 0
				? static_cast<ch_cnt_t>(std::min<ch_cnt_t>(physicalChannels, DEFAULT_CHANNELS))
				: static_cast<ch_cnt_t>(physicalChannels - DEFAULT_CHANNELS);
		});
	buffer->allocateInterleavedBuffer();
	return buffer;
}

/**
 * @brief Returns a view of the logical channels of a channel buffer.
 *
 * The buffer may reserve a silent second channel for mono layouts and extra
 * physical storage for effects. Routing and metering must only see the
 * channels described by @a layout.
 */
inline auto logicalBuffers(AudioBuffer& buffer, const AudioChannelLayout& layout)
	-> PlanarBufferView<float>
{
	return {buffer.allBuffers().data(), std::min(layout.channels(), buffer.totalChannels()),
		buffer.frames()};
}

//! @copydoc logicalBuffers(AudioBuffer&, const AudioChannelLayout&)
inline auto logicalBuffers(const AudioBuffer& buffer, const AudioChannelLayout& layout)
	-> PlanarBufferView<const float>
{
	return {buffer.allBuffers().data(), std::min(layout.channels(), buffer.totalChannels()),
		buffer.frames()};
}

} // namespace mxm

#endif // MXM_AUDIO_CHANNEL_BUFFER_H
