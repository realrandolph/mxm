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

/**
 * @brief A selectable capture endpoint for a recordable track.
 *
 * `captureChannels` maps each source channel to a channel in the engine's
 * current capture snapshot. Keeping the mapping explicit (instead of assuming
 * adjacent stereo pairs) lets a track record nonadjacent interface channels
 * today and arbitrary layouts later.
 *
 * A track-port source is provided by a backend that can route a named source
 * to one track (currently JACK). Its `endpointPorts` identify the source and
 * its `captureChannels` index the track's capture snapshot.
 */
struct MXM_EXPORT AudioInputSource
{
	QString id;
	QString displayName;
	ch_cnt_t channels = 0;
	AudioChannelLayout layout;
	std::vector<ch_cnt_t> captureChannels;
	bool trackPort = false;
	QStringList endpointPorts;

	auto valid() const -> bool
	{
		return channels > 0 && layout.channels() == channels
			&& captureChannels.size() == channels;
	}
};

/**
 * @brief Builds selectable mono, stereo-pair and all-channel sources.
 *
 * The first entry is always "Auto" for the complete configured capture
 * layout. Mono inputs and adjacent stereo pairs follow, then an explicit
 * all-channel entry.
 */
MXM_EXPORT auto makeDeviceInputSources(ch_cnt_t deviceChannels,
	const AudioChannelLayout& deviceLayout, const QString& deviceName = {})
	-> std::vector<AudioInputSource>;

/**
 * @brief Validates an assignment against the current device snapshot.
 *
 * @param source selected source
 * @param overrideChannels optional channel map; when empty, the source's own
 *        captureChannels are used
 * @param deviceChannels current engine capture channel count
 * @returns true when every selected channel is available
 */
MXM_EXPORT auto validateInputSource(const AudioInputSource& source,
	const std::vector<ch_cnt_t>& overrideChannels, ch_cnt_t deviceChannels) -> bool;

//! @returns the effective channel map for an assignment.
MXM_EXPORT auto inputSourceChannels(const AudioInputSource& source,
	const std::vector<ch_cnt_t>& overrideChannels) -> const std::vector<ch_cnt_t>&;

/**
 * @brief Copies selected source channels from the device capture snapshot.
 *
 * @param deviceInput planar snapshot as returned by AudioEngine::inputBuffer()
 * @param source source assignment
 * @param overrideChannels optional channel map; empty uses source.captureChannels
 * @param destination planar destination with `channels` buffers
 * @param gain constant gain applied while copying
 * @returns true when the mapping was valid
 */
MXM_EXPORT auto copyInputSource(PlanarBufferView<const float> deviceInput,
	const AudioInputSource& source, const std::vector<ch_cnt_t>& overrideChannels,
	PlanarBufferView<float> destination, float gain = 1.0f) -> bool;

/**
 * @brief Creates a non-owning planar view of the selected source channels.
 *
 * @param deviceInput planar snapshot
 * @param source source assignment
 * @param overrideChannels optional channel map
 * @param pointers storage for `source.channels` non-owning channel pointers
 * @returns a view valid while @a deviceInput and @a source are unchanged
 */
MXM_EXPORT auto inputSourceView(PlanarBufferView<const float> deviceInput,
	const AudioInputSource& source, const std::vector<ch_cnt_t>& overrideChannels,
	std::vector<const float*>& pointers) -> PlanarBufferView<const float>;

} // namespace mxm

#endif // MXM_AUDIO_INPUT_SOURCE_H
