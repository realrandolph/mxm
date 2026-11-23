/*
 * AudioInputSource.h - per-track audio input endpoints
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

#ifndef MXM_AUDIO_INPUT_SOURCE_H
#define MXM_AUDIO_INPUT_SOURCE_H

#include <vector>

#include <QString>
#include <QStringList>

#include "AudioBufferView.h"
#include "AudioChannelLayout.h"
#include "MxmTypes.h"
#include "mxm_export.h"

namespace mxm
{

inline constexpr auto SystemDefaultInputSourceId = "system-default";
//! Synthetic source for a track's own ports, patched externally rather than opened.
inline constexpr auto TrackInputSourceId = "track";

/**
 * @brief A selectable capture endpoint for a recordable track.
 *
 * `endpointPorts` identifies the backend source to open or connect.
 */
struct MXM_EXPORT AudioInputSource
{
	QString id;
	QString displayName;
	ch_cnt_t channels = 0;
	AudioChannelLayout layout;
	QStringList endpointPorts;
	//! The backend reports the selected source's actual channels after opening it.
	bool channelsFollowCapture = false;

	auto valid() const -> bool
	{
		return channels > 0 && layout.channels() == channels;
	}
};

MXM_EXPORT auto makeAudioInputSource(QString id, QString displayName,
	ch_cnt_t channels, QStringList endpointPorts, bool channelsFollowCapture = false) -> AudioInputSource;

/**
 * @brief Views selected channels in one track's capture block.
 *
 * @param deviceInput planar per-track capture block
 * @param source source assignment
 * @param firstChannel first source channel to expose
 * @param pointers storage for `source.channels` non-owning channel pointers
 * @returns a view valid while @a deviceInput and @a source are unchanged
 */
MXM_EXPORT auto inputSourceView(PlanarBufferView<const float> deviceInput,
	const AudioInputSource& source, ch_cnt_t firstChannel,
	std::vector<const float*>& pointers)
	-> PlanarBufferView<const float>;

} // namespace mxm

#endif // MXM_AUDIO_INPUT_SOURCE_H
