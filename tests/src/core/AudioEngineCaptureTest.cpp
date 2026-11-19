/*
 * AudioEngineCaptureTest.cpp - publishing captured audio to the audio engine
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
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#include "AudioEngine.h"

#include <QtTest>

#include <cstdint>
#include <vector>

#include "Engine.h"

using namespace mxm;

namespace
{

//! Identifies both the source frame and channel, and is exactly representable
auto inputValue(f_cnt_t frame, ch_cnt_t channel) -> float
{
	return static_cast<float>(frame) * 100.f + static_cast<float>(channel) + 1.f;
}

auto channelPointers(const std::vector<std::vector<float>>& block) -> std::vector<const float*>
{
	auto pointers = std::vector<const float*>(block.size());
	for (size_t channel = 0; channel < block.size(); ++channel)
	{
		pointers[channel] = block[channel].data();
	}
	return pointers;
}

} // namespace

class AudioEngineCaptureTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase() { Engine::init(true); }

	void cleanupTestCase() { Engine::destroy(); }

	//! Input channels are preallocated, clamped and can be disabled again
	void InputChannels_ConfiguredAndClamped()
	{
		auto* engine = Engine::audioEngine();
		QVERIFY(engine != nullptr);

		auto guard = engine->requestChangesGuard();

		engine->setInputChannels(8);
		QCOMPARE(engine->inputChannels(), ch_cnt_t{8});
		QCOMPARE(engine->inputBuffer().channels(), ch_cnt_t{8});

		engine->setInputChannels(static_cast<ch_cnt_t>(MaxChannelsPerAudioBuffer + 32));
		QCOMPARE(engine->inputChannels(), MaxChannelsPerAudioBuffer);

		engine->setInputChannels(0);
		QCOMPARE(engine->inputChannels(), ch_cnt_t{0});
		QVERIFY(engine->inputBuffer().empty());

		engine->setInputChannels(8);
		QCOMPARE(engine->inputChannels(), ch_cnt_t{8});
	}

	//! A planar block is published with all channels and their values intact
	void PushInputFrames_PlanarPreservesChannels()
	{
		constexpr auto frames = f_cnt_t{64};
		constexpr auto channels = ch_cnt_t{8};
		auto* engine = Engine::audioEngine();
		QVERIFY(engine != nullptr);

		// Hold the model change guard so the audio device cannot swap the
		// capture buffers while the block is published and read back.
		auto guard = engine->requestChangesGuard();
		engine->setInputChannels(channels);

		auto block = std::vector<std::vector<float>>(channels, std::vector<float>(frames));
		for (ch_cnt_t channel = 0; channel < channels; ++channel)
		{
			for (f_cnt_t frame = 0; frame < frames; ++frame)
			{
				block[channel][frame] = inputValue(frame, channel);
			}
		}
		const auto pointers = channelPointers(block);
		const auto droppedBefore = engine->inputDroppedFrames();

		engine->pushInputFrames(PlanarBufferView<const float>{pointers.data(), channels, frames});
		engine->renderNextPeriod();

		const auto input = engine->inputBuffer();
		QCOMPARE(input.channels(), channels);
		QCOMPARE(input.frames(), frames);
		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			for (ch_cnt_t channel = 0; channel < channels; ++channel)
			{
				QVERIFY2(input.bufferPtr(channel)[frame] == inputValue(frame, channel),
					qPrintable(QStringLiteral("frame %1 channel %2").arg(frame).arg(channel)));
			}
		}

		QCOMPARE(engine->inputDroppedFrames(), droppedBefore);
		QVERIFY(engine->inputPeak() > 0.f);
	}

	//! An interleaved block is published with all channels and their values intact
	void PushInputFrames_InterleavedPreservesChannels()
	{
		constexpr auto frames = f_cnt_t{32};
		constexpr auto channels = ch_cnt_t{8};
		auto* engine = Engine::audioEngine();
		QVERIFY(engine != nullptr);

		auto guard = engine->requestChangesGuard();
		engine->setInputChannels(channels);

		auto interleaved = std::vector<float>(frames * channels);
		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			for (ch_cnt_t channel = 0; channel < channels; ++channel)
			{
				interleaved[frame * channels + channel] = inputValue(frame, channel);
			}
		}
		const auto droppedBefore = engine->inputDroppedFrames();

		engine->pushInputFrames(
			InterleavedBufferView<const float>{interleaved.data(), channels, frames});
		engine->renderNextPeriod();

		const auto input = engine->inputBuffer();
		QCOMPARE(input.channels(), channels);
		QCOMPARE(input.frames(), frames);
		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			for (ch_cnt_t channel = 0; channel < channels; ++channel)
			{
				QVERIFY2(input.bufferPtr(channel)[frame] == inputValue(frame, channel),
					qPrintable(QStringLiteral("frame %1 channel %2").arg(frame).arg(channel)));
			}
		}

		QCOMPARE(engine->inputDroppedFrames(), droppedBefore);
	}

	//! A block with the wrong channel count is reported, not silently mixed
	void PushInputFrames_ChannelMismatchIsReported()
	{
		constexpr auto frames = f_cnt_t{64};
		auto* engine = Engine::audioEngine();
		QVERIFY(engine != nullptr);

		auto guard = engine->requestChangesGuard();
		engine->setInputChannels(8);

		// Wrong planar channel count
		auto planarBlock = std::vector<std::vector<float>>(2, std::vector<float>(frames, 0.5f));
		const auto planarPointers = channelPointers(planarBlock);
		auto droppedBefore = engine->inputDroppedFrames();
		engine->pushInputFrames(PlanarBufferView<const float>{planarPointers.data(), 2, frames});
		QCOMPARE(engine->inputDroppedFrames() - droppedBefore, static_cast<std::uint64_t>(frames));

		// Wrong interleaved channel count
		const auto interleaved = std::vector<float>(frames * 2, 0.5f);
		droppedBefore = engine->inputDroppedFrames();
		engine->pushInputFrames(InterleavedBufferView<const float>{interleaved.data(), 2, frames});
		QCOMPARE(engine->inputDroppedFrames() - droppedBefore, static_cast<std::uint64_t>(frames));

		// Empty input is ignored
		droppedBefore = engine->inputDroppedFrames();
		engine->pushInputFrames(PlanarBufferView<const float>{});
		QCOMPARE(engine->inputDroppedFrames(), droppedBefore);
	}
};

QTEST_GUILESS_MAIN(AudioEngineCaptureTest)
#include "AudioEngineCaptureTest.moc"
