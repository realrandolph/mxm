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
 *
 */

#include "AudioInputSource.h"

#include <algorithm>
#include <QObject>

namespace mxm
{

namespace
{

auto devicePrefix(const QString& deviceName) -> QString
{
	return deviceName.isEmpty() ? QString{} : deviceName + QStringLiteral(" ");
}

} // namespace

auto makeDeviceInputSources(ch_cnt_t deviceChannels, const AudioChannelLayout& deviceLayout,
	const QString& deviceName) -> std::vector<AudioInputSource>
{
	auto sources = std::vector<AudioInputSource>{};
	if (deviceChannels == 0) { return sources; }

	const auto prefix = devicePrefix(deviceName);
	const auto allLayout = deviceLayout.channels() == deviceChannels
		? deviceLayout : AudioChannelLayout::discrete(deviceChannels);

	auto allChannels = std::vector<ch_cnt_t>(deviceChannels);
	for (ch_cnt_t channel = 0; channel < deviceChannels; ++channel)
	{
		allChannels[channel] = channel;
	}

	sources.push_back(AudioInputSource{
		QStringLiteral("auto"),
		prefix + QObject::tr("Auto (all inputs)"),
		deviceChannels,
		allLayout,
		allChannels,
		false,
	});

	for (ch_cnt_t channel = 0; channel < deviceChannels; ++channel)
	{
		sources.push_back(AudioInputSource{
			QStringLiteral("device:%1").arg(channel),
			prefix + QObject::tr("Input %1").arg(channel + 1),
			1,
			AudioChannelLayout::mono(),
			{channel},
			false,
		});
	}

	for (ch_cnt_t channel = 0; channel + 1 < deviceChannels; ++channel)
	{
		sources.push_back(AudioInputSource{
			QStringLiteral("device:%1+%2").arg(channel).arg(channel + 1),
			prefix + QObject::tr("Inputs %1 + %2 (stereo)").arg(channel + 1).arg(channel + 2),
			2,
			AudioChannelLayout::stereo(),
			{channel, static_cast<ch_cnt_t>(channel + 1)},
			false,
		});
	}

	return sources;
}

auto validateInputSource(const AudioInputSource& source,
	const std::vector<ch_cnt_t>& overrideChannels, ch_cnt_t deviceChannels) -> bool
{
	if (source.trackPort) { return source.valid(); }
	if (!source.valid()) { return false; }

	const auto& channels = inputSourceChannels(source, overrideChannels);
	if (channels.size() != source.channels) { return false; }

	return std::ranges::all_of(channels, [deviceChannels](ch_cnt_t channel) {
		return channel < deviceChannels;
	});
}

auto inputSourceChannels(const AudioInputSource& source,
	const std::vector<ch_cnt_t>& overrideChannels) -> const std::vector<ch_cnt_t>&
{
	return overrideChannels.empty() ? source.captureChannels : overrideChannels;
}

auto copyInputSource(PlanarBufferView<const float> deviceInput,
	const AudioInputSource& source, const std::vector<ch_cnt_t>& overrideChannels,
	PlanarBufferView<float> destination, float gain) -> bool
{
	if (source.trackPort) { return false; }
	if (deviceInput.empty() || source.channels == 0) { return false; }

	const auto& channels = inputSourceChannels(source, overrideChannels);
	if (channels.size() != source.channels
		|| destination.channels() < source.channels)
	{
		return false;
	}

	if (std::ranges::any_of(channels, [&](ch_cnt_t channel) {
		return channel >= deviceInput.channels();
	}))
	{
		return false;
	}

	const auto frames = std::min(deviceInput.frames(), destination.frames());
	for (ch_cnt_t channel = 0; channel < source.channels; ++channel)
	{
		const auto* sourceData = deviceInput.bufferPtr(channels[channel]);
		auto* destinationData = destination.bufferPtr(channel);
		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			destinationData[frame] = sourceData[frame] * gain;
		}
	}

	return true;
}

auto inputSourceView(PlanarBufferView<const float> deviceInput,
	const AudioInputSource& source, const std::vector<ch_cnt_t>& overrideChannels,
	std::vector<const float*>& pointers) -> PlanarBufferView<const float>
{
	pointers.clear();
	if (deviceInput.empty() || source.channels == 0) { return {}; }

	const auto& channels = inputSourceChannels(source, overrideChannels);
	if (channels.size() != source.channels) { return {}; }

	if (std::ranges::any_of(channels, [&](ch_cnt_t channel) {
		return channel >= deviceInput.channels();
	}))
	{
		pointers.clear();
		return {};
	}

	pointers.reserve(source.channels);
	for (const auto channel : channels)
	{
		pointers.push_back(deviceInput.bufferPtr(channel));
	}

	return {pointers.data(), source.channels, deviceInput.frames()};
}

} // namespace mxm
