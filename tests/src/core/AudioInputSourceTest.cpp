/*
 * AudioInputSourceTest.cpp - per-track audio input endpoints
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

#include <QtTest>

#include <vector>

using namespace mxm;

namespace
{

constexpr auto DeviceChannels = ch_cnt_t{8};
constexpr auto DeviceFrames = f_cnt_t{16};

//! A snapshot whose samples identify both the source channel and the frame
auto makeSnapshot(ch_cnt_t channels, f_cnt_t frames) -> std::vector<std::vector<float>>
{
	auto snapshot = std::vector<std::vector<float>>(channels, std::vector<float>(frames));
	for (ch_cnt_t channel = 0; channel < channels; ++channel)
	{
		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			snapshot[channel][frame] = static_cast<float>(channel * 1000 + frame);
		}
	}
	return snapshot;
}

auto constPointers(const std::vector<std::vector<float>>& block) -> std::vector<const float*>
{
	auto pointers = std::vector<const float*>(block.size());
	for (size_t channel = 0; channel < block.size(); ++channel)
	{
		pointers[channel] = block[channel].data();
	}
	return pointers;
}

auto mutablePointers(std::vector<std::vector<float>>& block) -> std::vector<float*>
{
	auto pointers = std::vector<float*>(block.size());
	for (size_t channel = 0; channel < block.size(); ++channel)
	{
		pointers[channel] = block[channel].data();
	}
	return pointers;
}

} // namespace

class AudioInputSourceTest : public QObject
{
	Q_OBJECT

private slots:
	//! An 8-channel device yields auto, 8 mono and 7 adjacent stereo sources
	void MakeDeviceInputSources_AutoMonoAndPairs()
	{
		const auto layout = AudioChannelLayout::discrete(DeviceChannels);
		const auto sources = makeDeviceInputSources(
			DeviceChannels, layout, QStringLiteral("Test Device"));

		QCOMPARE(sources.size(), size_t{16});

		const auto& automatic = sources[0];
		QCOMPARE(automatic.id, QStringLiteral("auto"));
		QCOMPARE(automatic.channels, DeviceChannels);
		QCOMPARE(automatic.layout, layout);
		QCOMPARE(automatic.captureChannels,
			(std::vector<ch_cnt_t>{0, 1, 2, 3, 4, 5, 6, 7}));
		QCOMPARE(automatic.displayName, QStringLiteral("Test Device Auto (all inputs)"));
		QVERIFY(automatic.valid());
		QVERIFY(!automatic.trackPort);

		for (ch_cnt_t channel = 0; channel < DeviceChannels; ++channel)
		{
			const auto& mono = sources[1 + channel];
			QCOMPARE(mono.id, QStringLiteral("device:%1").arg(channel));
			QCOMPARE(mono.channels, ch_cnt_t{1});
			QCOMPARE(mono.layout, AudioChannelLayout::mono());
			QCOMPARE(mono.captureChannels, (std::vector<ch_cnt_t>{channel}));
			QVERIFY(mono.valid());
			QVERIFY(!mono.trackPort);
		}

		QCOMPARE(sources[1].displayName, QStringLiteral("Test Device Input 1"));
		QCOMPARE(sources[8].displayName, QStringLiteral("Test Device Input 8"));

		for (ch_cnt_t channel = 0; channel + 1 < DeviceChannels; ++channel)
		{
			const auto& pair = sources[9 + channel];
			QCOMPARE(pair.id, QStringLiteral("device:%1+%2").arg(channel).arg(channel + 1));
			QCOMPARE(pair.channels, ch_cnt_t{2});
			QCOMPARE(pair.layout, AudioChannelLayout::stereo());
			QCOMPARE(pair.captureChannels,
				(std::vector<ch_cnt_t>{channel, static_cast<ch_cnt_t>(channel + 1)}));
			QVERIFY(pair.valid());
			QVERIFY(!pair.trackPort);
		}

		QCOMPARE(sources[9].displayName, QStringLiteral("Test Device Inputs 1 + 2 (stereo)"));
		QCOMPARE(sources[15].displayName, QStringLiteral("Test Device Inputs 7 + 8 (stereo)"));
	}

	//! Assignments are accepted only for complete, in-range channel maps
	void Validate_DeviceSnapshot()
	{
		const auto sources = makeDeviceInputSources(
			DeviceChannels, AudioChannelLayout::discrete(DeviceChannels));
		const auto& mono = sources[1];
		const auto& pair = sources[9];

		QVERIFY(validateInputSource(mono, {}, DeviceChannels));
		QVERIFY(validateInputSource(mono, {7}, DeviceChannels));
		QVERIFY(validateInputSource(pair, {}, DeviceChannels));
		QVERIFY(validateInputSource(pair, {6, 7}, DeviceChannels));

		// A channel equal to the device channel count is not available
		QVERIFY(!validateInputSource(mono, {DeviceChannels}, DeviceChannels));
		QVERIFY(!validateInputSource(pair, {0, DeviceChannels}, DeviceChannels));

		// An override must provide exactly one channel per source channel
		QVERIFY(!validateInputSource(mono, {0, 1}, DeviceChannels));
		QVERIFY(!validateInputSource(pair, {0}, DeviceChannels));
		QVERIFY(!validateInputSource(pair, {0, 1, 2}, DeviceChannels));

		// Invalid sources are rejected even when the device can satisfy them
		auto invalidSource = mono;
		invalidSource.captureChannels.clear();
		QVERIFY(!validateInputSource(invalidSource, {}, DeviceChannels));

		auto layoutMismatch = mono;
		layoutMismatch.layout = AudioChannelLayout::stereo();
		QVERIFY(!validateInputSource(layoutMismatch, {}, DeviceChannels));

		QVERIFY(!validateInputSource(mono, {}, ch_cnt_t{0}));
	}

	//! Copying a nonadjacent selection uses exactly the requested channels
	void Copy_NonAdjacentChannels()
	{
		const auto snapshot = makeSnapshot(DeviceChannels, DeviceFrames);
		const auto devicePointers = constPointers(snapshot);
		const auto deviceInput = PlanarBufferView<const float>{
			devicePointers.data(), DeviceChannels, DeviceFrames};

		const auto source = AudioInputSource{
			QStringLiteral("device:2+6"), QStringLiteral("Inputs 3 + 7"),
			ch_cnt_t{2}, AudioChannelLayout::stereo(), {2, 6}, false};

		auto destination = std::vector<std::vector<float>>(
			2, std::vector<float>(DeviceFrames, -1.f));
		auto destinationPointers = mutablePointers(destination);
		const auto destinationView = PlanarBufferView<float>{
			destinationPointers.data(), ch_cnt_t{2}, DeviceFrames};

		QVERIFY(copyInputSource(deviceInput, source, {}, destinationView));
		for (f_cnt_t frame = 0; frame < DeviceFrames; ++frame)
		{
			QCOMPARE(destination[0][frame], snapshot[2][frame]);
			QCOMPARE(destination[1][frame], snapshot[6][frame]);
		}
	}

	//! Out-of-range selections fail without falling back to other channels
	void Copy_OutOfRangeFailsWithoutSubstitution()
	{
		const auto snapshot = makeSnapshot(DeviceChannels, DeviceFrames);
		const auto devicePointers = constPointers(snapshot);
		const auto deviceInput = PlanarBufferView<const float>{
			devicePointers.data(), DeviceChannels, DeviceFrames};

		const auto source = AudioInputSource{
			QStringLiteral("device:2+6"), QStringLiteral("Inputs 3 + 7"),
			ch_cnt_t{2}, AudioChannelLayout::stereo(), {2, 6}, false};

		auto destination = std::vector<std::vector<float>>(
			2, std::vector<float>(DeviceFrames, -1.f));
		auto destinationPointers = mutablePointers(destination);
		const auto destinationView = PlanarBufferView<float>{
			destinationPointers.data(), ch_cnt_t{2}, DeviceFrames};

		QVERIFY(!copyInputSource(deviceInput, source, {2, DeviceChannels}, destinationView));
		for (const auto& channel : destination)
		{
			for (const auto sample : channel) { QCOMPARE(sample, -1.f); }
		}
	}

	//! Views expose pointers to the selected source channels only
	void View_SelectsRequestedChannels()
	{
		const auto snapshot = makeSnapshot(DeviceChannels, DeviceFrames);
		const auto devicePointers = constPointers(snapshot);
		const auto deviceInput = PlanarBufferView<const float>{
			devicePointers.data(), DeviceChannels, DeviceFrames};

		const auto source = AudioInputSource{
			QStringLiteral("device:6+2"), QStringLiteral("Inputs 7 + 3"),
			ch_cnt_t{2}, AudioChannelLayout::stereo(), {6, 2}, false};

		auto pointers = std::vector<const float*>{};
		const auto view = inputSourceView(deviceInput, source, {}, pointers);

		QCOMPARE(view.channels(), ch_cnt_t{2});
		QCOMPARE(view.frames(), DeviceFrames);
		QCOMPARE(pointers.size(), size_t{2});
		QCOMPARE(pointers[0], snapshot[6].data());
		QCOMPARE(pointers[1], snapshot[2].data());
		for (f_cnt_t frame = 0; frame < DeviceFrames; ++frame)
		{
			QCOMPARE(view.sample(0, frame), snapshot[6][frame]);
			QCOMPARE(view.sample(1, frame), snapshot[2][frame]);
		}

		auto invalidPointers = std::vector<const float*>{};
		const auto invalidView = inputSourceView(
			deviceInput, source, {6, DeviceChannels}, invalidPointers);
		QVERIFY(invalidView.empty());
		QVERIFY(invalidPointers.empty());
	}
};

QTEST_GUILESS_MAIN(AudioInputSourceTest)
#include "AudioInputSourceTest.moc"
