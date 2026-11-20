/*
 * SampleCaptureTest.cpp - channel-independent capture queue
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

#include "SampleCapture.h"

#include <QtTest>

#include <cstdint>
#include <vector>

#include "Engine.h"

using namespace mxm;

namespace
{

constexpr auto TestSampleRate = sample_rate_t{48000};

//! A block whose samples identify both the block and the channel
auto makePlanarBlock(ch_cnt_t channels, f_cnt_t frames, size_t blockIndex)
	-> std::vector<std::vector<float>>
{
	auto block = std::vector<std::vector<float>>(channels, std::vector<float>(frames));
	for (ch_cnt_t channel = 0; channel < channels; ++channel)
	{
		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			block[channel][frame] = static_cast<float>(blockIndex * 1000 + channel * 100 + frame);
		}
	}
	return block;
}

auto blockPointers(const std::vector<std::vector<float>>& block) -> std::vector<const float*>
{
	auto pointers = std::vector<const float*>(block.size());
	for (size_t channel = 0; channel < block.size(); ++channel)
	{
		pointers[channel] = block[channel].data();
	}
	return pointers;
}

auto expectedSample(size_t blockIndex, ch_cnt_t channel, f_cnt_t frameInBlock) -> float
{
	return static_cast<float>(blockIndex * 1000 + channel * 100 + frameInBlock);
}

} // namespace

class SampleCaptureTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		// Capture finalization constructs `SampleBuffer`, whose default sample
		// rate and empty buffer use Engine.
		Engine::init(true);
	}

	void cleanupTestCase() { Engine::destroy(); }

	//! Mono, stereo, 5.1 and 7.1 planar takes keep every channel in order
	void Capture_PlanarLayouts_data()
	{
		QTest::addColumn<int>("channels");
		QTest::newRow("mono") << 1;
		QTest::newRow("stereo") << 2;
		QTest::newRow("5.1") << 6;
		QTest::newRow("7.1") << 8;
	}

	void Capture_PlanarLayouts()
	{
		QFETCH(int, channels);

		constexpr auto blockFrames = f_cnt_t{64};
		constexpr auto blockCount = size_t{64};
		constexpr auto blockTotal = size_t{4};
		const auto channelCount = static_cast<ch_cnt_t>(channels);
		const auto layout = AudioChannelLayout::standard(channelCount);
		QCOMPARE(layout.channels(), channelCount);

		auto capture = SampleCapture{TestSampleRate, layout, blockFrames, blockCount};

		for (size_t blockIndex = 0; blockIndex < blockTotal; ++blockIndex)
		{
			const auto block = makePlanarBlock(channelCount, blockFrames, blockIndex);
			const auto pointers = blockPointers(block);
			capture.write(PlanarBufferView<const float>{pointers.data(), channelCount, blockFrames});
		}

		// The whole take fits the preallocated queue
		QCOMPARE(capture.recordedFrames(), static_cast<std::uint64_t>(blockTotal * blockFrames));
		QCOMPARE(capture.droppedFrames(), std::uint64_t{0});
		QVERIFY(!capture.diskError());

		const auto result = capture.finish();
		QVERIFY(result != nullptr);
		QVERIFY(!result->empty());
		QCOMPARE(result->channels(), channelCount);
		QCOMPARE(result->layout().channels(), channelCount);
		QVERIFY(result->layout() == layout);
		QCOMPARE(result->sampleRate(), TestSampleRate);
		QCOMPARE(result->frames(), static_cast<f_cnt_t>(blockTotal * blockFrames));

		for (f_cnt_t frame = 0; frame < result->frames(); ++frame)
		{
			const auto blockIndex = static_cast<size_t>(frame / blockFrames);
			const auto frameInBlock = static_cast<f_cnt_t>(frame % blockFrames);
			for (ch_cnt_t channel = 0; channel < channelCount; ++channel)
			{
				const auto expected = expectedSample(blockIndex, channel, frameInBlock);
				QVERIFY2(result->sample(frame, channel) == expected,
					qPrintable(QStringLiteral("frame %1 channel %2").arg(frame).arg(channel)));
			}
		}
	}

	//! Interleaved writes preserve channel order as well
	void Capture_Interleaved()
	{
		constexpr auto blockFrames = f_cnt_t{32};
		constexpr auto blockTotal = size_t{2};
		const auto layout = AudioChannelLayout::surround51();
		const auto channels = layout.channels();

		auto capture = SampleCapture{44100, layout, blockFrames, size_t{8}};

		for (size_t blockIndex = 0; blockIndex < blockTotal; ++blockIndex)
		{
			auto interleaved = std::vector<float>(blockFrames * channels);
			for (f_cnt_t frame = 0; frame < blockFrames; ++frame)
			{
				for (ch_cnt_t channel = 0; channel < channels; ++channel)
				{
					interleaved[frame * channels + channel]
						= expectedSample(blockIndex, channel, frame);
				}
			}
			capture.write(InterleavedBufferView<const float>{interleaved.data(), channels, blockFrames});
		}

		QCOMPARE(capture.recordedFrames(), static_cast<std::uint64_t>(blockTotal * blockFrames));
		QCOMPARE(capture.droppedFrames(), std::uint64_t{0});

		const auto result = capture.finish();
		QVERIFY(result != nullptr);
		QVERIFY(!result->empty());
		QCOMPARE(result->channels(), channels);
		QVERIFY(result->layout() == layout);
		QCOMPARE(result->frames(), static_cast<f_cnt_t>(blockTotal * blockFrames));

		for (f_cnt_t frame = 0; frame < result->frames(); ++frame)
		{
			const auto blockIndex = static_cast<size_t>(frame / blockFrames);
			const auto frameInBlock = static_cast<f_cnt_t>(frame % blockFrames);
			for (ch_cnt_t channel = 0; channel < channels; ++channel)
			{
				QVERIFY(result->sample(frame, channel)
					== expectedSample(blockIndex, channel, frameInBlock));
			}
		}
	}

	//! A mapped mono source lands in the requested capture channel only
	void Capture_MappedMonoDestination()
	{
		constexpr auto blockFrames = f_cnt_t{16};
		constexpr auto frames = f_cnt_t{32};
		const auto layout = AudioChannelLayout::stereo();

		auto capture = SampleCapture{TestSampleRate, layout, blockFrames, size_t{8}};

		const auto block = makePlanarBlock(1, frames, 0);
		const auto pointers = blockPointers(block);
		capture.write(
			PlanarBufferView<const float>{pointers.data(), ch_cnt_t{1}, frames},
			std::vector<ch_cnt_t>{1});

		QCOMPARE(capture.recordedFrames(), static_cast<std::uint64_t>(frames));
		QCOMPARE(capture.droppedFrames(), std::uint64_t{0});
		QVERIFY(!capture.diskError());

		const auto result = capture.finish();
		QVERIFY(result != nullptr);
		QVERIFY(!result->empty());
		QCOMPARE(result->channels(), ch_cnt_t{2});
		QCOMPARE(result->layout(), layout);
		QCOMPARE(result->frames(), frames);

		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			QCOMPARE(result->sample(frame, 0), 0.f);
			QCOMPARE(result->sample(frame, 1), expectedSample(0, 0, frame));
		}
	}

	//! Destination maps reorder source channels and silence unmapped ones
	void Capture_MappedDestinationsReorder()
	{
		constexpr auto blockFrames = f_cnt_t{16};
		constexpr auto frames = f_cnt_t{32};
		const auto layout = AudioChannelLayout::stereo();

		auto capture = SampleCapture{TestSampleRate, layout, blockFrames, size_t{8}};

		const auto block = makePlanarBlock(2, frames, 0);
		const auto pointers = blockPointers(block);
		capture.write(
			PlanarBufferView<const float>{pointers.data(), ch_cnt_t{2}, frames},
			std::vector<ch_cnt_t>{1, 0});

		QCOMPARE(capture.recordedFrames(), static_cast<std::uint64_t>(frames));
		QCOMPARE(capture.droppedFrames(), std::uint64_t{0});

		const auto result = capture.finish();
		QVERIFY(result != nullptr);
		QVERIFY(!result->empty());
		QCOMPARE(result->channels(), ch_cnt_t{2});
		QCOMPARE(result->frames(), frames);

		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			QCOMPARE(result->sample(frame, 0), expectedSample(0, 1, frame));
			QCOMPARE(result->sample(frame, 1), expectedSample(0, 0, frame));
		}
	}

	//! Overflow is reported instead of silently discarding frames
	void Overflow_AccountsForEveryFrame()
	{
		constexpr auto blockFrames = f_cnt_t{256};
		constexpr auto blockCount = size_t{2};
		constexpr auto writeTotal = size_t{400};
		const auto layout = AudioChannelLayout::surround71();
		const auto channels = layout.channels();

		auto capture = SampleCapture{TestSampleRate, layout, blockFrames, blockCount};

		const auto block = std::vector<std::vector<float>>(
			channels, std::vector<float>(blockFrames, 0.5f));
		const auto pointers = blockPointers(block);

		// Write far more frames than the queue can hold without waiting for the
		// asynchronous writer to drain.
		for (size_t write = 0; write < writeTotal; ++write)
		{
			capture.write(PlanarBufferView<const float>{pointers.data(), channels, blockFrames});
		}

		const auto totalFrames = static_cast<std::uint64_t>(writeTotal) * blockFrames;
		QCOMPARE(capture.recordedFrames() + capture.droppedFrames(), totalFrames);
		QVERIFY(capture.recordedFrames() <= totalFrames);
		QVERIFY(!capture.diskError());

		const auto result = capture.finish();
		QVERIFY(result != nullptr);
		QVERIFY(!capture.diskError());
		if (!result->empty())
		{
			QCOMPARE(result->frames(), capture.recordedFrames());
			QCOMPARE(result->channels(), channels);
			QVERIFY(result->layout() == layout);
			for (f_cnt_t frame = 0; frame < result->frames(); ++frame)
			{
				for (ch_cnt_t channel = 0; channel < channels; ++channel)
				{
					QVERIFY(result->sample(frame, channel) == 0.5f);
				}
			}
		}
	}

	//! `finish()` may only produce a take once
	void Finish_IsIdempotent()
	{
		constexpr auto blockFrames = f_cnt_t{64};
		const auto layout = AudioChannelLayout::stereo();
		auto capture = SampleCapture{44100, layout, blockFrames, size_t{8}};

		const auto block = makePlanarBlock(layout.channels(), blockFrames, 0);
		const auto pointers = blockPointers(block);
		capture.write(PlanarBufferView<const float>{pointers.data(), layout.channels(), blockFrames});

		const auto first = capture.finish();
		QVERIFY(first != nullptr);
		QVERIFY(!first->empty());
		QCOMPARE(first->frames(), blockFrames);

		const auto second = capture.finish();
		QVERIFY(second != nullptr);
		QVERIFY(second->empty());
		QCOMPARE(second->channels(), ch_cnt_t{0});
		QCOMPARE(second->frames(), f_cnt_t{0});

		const auto third = capture.finish();
		QVERIFY(third->empty());
	}

	//! A capture that never received frames finalizes to an empty buffer
	void Finish_WithoutWrites_ReturnsEmptyBuffer()
	{
		auto capture = SampleCapture{TestSampleRate, AudioChannelLayout::surround51(), f_cnt_t{32}, size_t{8}};
		const auto result = capture.finish();
		QVERIFY(result != nullptr);
		QVERIFY(result->empty());
		QCOMPARE(capture.recordedFrames(), std::uint64_t{0});
		QCOMPARE(capture.droppedFrames(), std::uint64_t{0});
	}
};

QTEST_GUILESS_MAIN(SampleCaptureTest)
#include "SampleCaptureTest.moc"
