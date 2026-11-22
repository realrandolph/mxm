/*
 * AudioInputSource.cpp - per-track audio input endpoints
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
 */

#include "AudioInputSource.h"

#include <utility>

namespace mxm
{

auto makeAudioInputSource(QString id, QString displayName, ch_cnt_t channels,
	QStringList endpointPorts) -> AudioInputSource
{
	const auto layout = channels == 1 ? AudioChannelLayout::mono()
		: channels == 2 ? AudioChannelLayout::stereo() : AudioChannelLayout::discrete(channels);
	return {std::move(id), std::move(displayName), channels, layout, std::move(endpointPorts)};
}

auto inputSourceView(PlanarBufferView<const float> input, const AudioInputSource& source,
	ch_cnt_t firstChannel, std::vector<const float*>& pointers) -> PlanarBufferView<const float>
{
	pointers.clear();
	if (!source.valid() || input.empty()
		|| firstChannel + source.channels > input.channels()) { return {}; }

	pointers.reserve(source.channels);
	for (ch_cnt_t channel = 0; channel < source.channels; ++channel)
	{
		pointers.push_back(input.bufferPtr(firstChannel + channel));
	}
	return {pointers.data(), source.channels, input.frames()};
}

} // namespace mxm
