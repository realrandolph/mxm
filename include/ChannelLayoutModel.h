/*
 * ChannelLayoutModel.h - combo box helpers for channel layouts
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

#ifndef MXM_CHANNEL_LAYOUT_MODEL_H
#define MXM_CHANNEL_LAYOUT_MODEL_H

#include <algorithm>
#include <vector>

#include "AudioChannelLayout.h"
#include "ComboBoxModel.h"

namespace mxm
{

/**
 * @brief Fills a combo box with the standard layouts plus @a current if needed.
 *
 * Custom/discrete layouts loaded from a project stay selectable instead of
 * being silently rounded to the nearest preset.
 *
 * @returns the combo box index of @a current.
 */
inline auto populateChannelLayoutModel(ComboBoxModel& model,
	std::vector<AudioChannelLayout>& choices, const AudioChannelLayout& current) -> int
{
	model.clear();
	choices.clear();

	for (const auto& preset : AudioChannelLayout::presets())
	{
		choices.push_back(preset.layout);
		model.addItem(preset.name);
	}

	auto index = std::find(choices.begin(), choices.end(), current) - choices.begin();
	if (index == static_cast<int>(choices.size()))
	{
		choices.push_back(current);
		model.addItem(current.displayName());
	}

	model.setValue(static_cast<int>(index));
	return static_cast<int>(index);
}

//! @returns the layout at @a index, or a stereo layout for invalid indices.
inline auto channelLayoutAt(const std::vector<AudioChannelLayout>& choices, int index)
	-> AudioChannelLayout
{
	return index >= 0 && static_cast<std::size_t>(index) < choices.size()
		? choices[index] : AudioChannelLayout::stereo();
}

} // namespace mxm

#endif // MXM_CHANNEL_LAYOUT_MODEL_H
