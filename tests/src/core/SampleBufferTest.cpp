/*
 * SampleBufferTest.cpp - multichannel sample storage and serialization
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

#include "SampleBuffer.h"
#include "Sample.h"

#include <QtTest>
#include <QTemporaryDir>

#include <array>
#include <stdexcept>
#include <vector>

#include "Engine.h"

using namespace mxm;

namespace
{

//! Identifies the source frame and channel while staying exactly representable
auto sampleValue(f_cnt_t frame, ch_cnt_t channel) -> float
{
	return static_cast<float>(frame) * 1000.f + static_cast<float>(channel) * 100.f + 0.5f;
}

auto makeInterleaved(f_cnt_t frames, ch_cnt_t channels) -> std::vector<float>
{
	auto samples = std::vector<float>(frames * channels);
	for (f_cnt_t frame = 0; frame < frames; ++frame)
	{
		for (ch_cnt_t channel = 0; channel < channels; ++channel)
		{
			samples[frame * channels + channel] = sampleValue(frame, channel);
		}
	}
	return samples;
}

auto makePlanar(f_cnt_t frames, ch_cnt_t channels) -> std::vector<std::vector<float>>
{
	auto planar = std::vector<std::vector<float>>(channels, std::vector<float>(frames));
	for (ch_cnt_t channel = 0; channel < channels; ++channel)
	{
		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			planar[channel][frame] = sampleValue(frame, channel);
		}
	}
	return planar;
}

auto planarPointers(const std::vector<std::vector<float>>& planar) -> std::vector<const float*>
{
	auto pointers = std::vector<const float*>(planar.size());
	for (size_t channel = 0; channel < planar.size(); ++channel)
	{
		pointers[channel] = planar[channel].data();
	}
	return pointers;
}

} // namespace

class SampleBufferTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		// `SampleBuffer::emptyBuffer()` and the default sample rate query use Engine
		Engine::init(true);
	}

	void cleanupTestCase() { Engine::destroy(); }

	//! Frame count, channel count, layout and sample rate are independent metadata
	void Metadata_ChannelCountsAndLayouts()
	{
		const auto cases = std::array<std::pair<ch_cnt_t, AudioChannelLayout>, 4>{{
			{1, AudioChannelLayout::mono()},
			{2, AudioChannelLayout::stereo()},
			{6, AudioChannelLayout::surround51()},
			{8, AudioChannelLayout::surround71()},
		}};

		for (const auto& [channels, layout] : cases)
		{
			constexpr auto frames = f_cnt_t{7};
			constexpr auto sampleRate = int{44100};
			const auto buffer
				= SampleBuffer{makeInterleaved(frames, channels), channels, layout, sampleRate};

			QCOMPARE(buffer.channels(), channels);
			QCOMPARE(buffer.layout().channels(), channels);
			QVERIFY(buffer.layout() == layout);
			QCOMPARE(buffer.frames(), frames);
			QCOMPARE(buffer.size(), static_cast<size_t>(frames));
			QCOMPARE(buffer.sampleRate(), static_cast<sample_rate_t>(sampleRate));
			QVERIFY(!buffer.empty());
			QCOMPARE(buffer.audioFile(), QString{});
		}

		// Discrete channel counts are distinct from the standard layouts
		const auto discrete = SampleBuffer{
			makeInterleaved(f_cnt_t{3}, ch_cnt_t{6}), ch_cnt_t{6}, AudioChannelLayout::discrete(6), 48000};
		QCOMPARE(discrete.channels(), ch_cnt_t{6});
		QVERIFY(discrete.layout().isDiscrete());
		QVERIFY(discrete.layout() != AudioChannelLayout::surround51());
	}

	//! Interleaved construction preserves frame-major sample order
	void Construction_Interleaved()
	{
		constexpr auto frames = f_cnt_t{5};
		constexpr auto channels = ch_cnt_t{6};
		constexpr auto sampleRate = int{48000};
		const auto layout = AudioChannelLayout::surround51();
		const auto expected = makeInterleaved(frames, channels);

		auto buffer = SampleBuffer{expected, channels, layout, sampleRate};

		QCOMPARE(buffer.channels(), channels);
		QCOMPARE(buffer.frames(), frames);
		QCOMPARE(buffer.interleavedView().channels(), channels);
		QCOMPARE(buffer.interleavedView().frames(), frames);

		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			for (ch_cnt_t channel = 0; channel < channels; ++channel)
			{
				QVERIFY(buffer.sample(frame, channel) == sampleValue(frame, channel));
				QVERIFY(buffer.interleavedData()[frame * channels + channel] == sampleValue(frame, channel));
			}
		}
	}

	//! Planar construction produces the same interleaved data as interleaved construction
	void Construction_Planar()
	{
		constexpr auto frames = f_cnt_t{5};
		constexpr auto channels = ch_cnt_t{8};
		constexpr auto sampleRate = int{44100};
		const auto layout = AudioChannelLayout::surround71();
		const auto planar = makePlanar(frames, channels);
		const auto pointers = planarPointers(planar);

		const auto buffer = SampleBuffer::fromPlanar(
			PlanarBufferView<const float>{pointers.data(), channels, frames}, layout, sampleRate);
		const auto expected = SampleBuffer::fromInterleaved(
			makeInterleaved(frames, channels).data(), frames, channels, layout, sampleRate);

		QVERIFY(buffer != nullptr);
		QVERIFY(!buffer->empty());
		QCOMPARE(buffer->channels(), channels);
		QCOMPARE(buffer->frames(), frames);
		QCOMPARE(buffer->sampleRate(), static_cast<sample_rate_t>(sampleRate));
		QVERIFY(buffer->layout() == layout);

		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			for (ch_cnt_t channel = 0; channel < channels; ++channel)
			{
				QVERIFY(buffer->sample(frame, channel) == expected->sample(frame, channel));
				QVERIFY(buffer->sample(frame, channel) == sampleValue(frame, channel));
			}
		}
	}

	//! Base64 serialization preserves data, channel count, layout and sample rate
	void Base64_RoundTrip()
	{
		const auto cases = std::array<std::pair<ch_cnt_t, AudioChannelLayout>, 5>{{
			{1, AudioChannelLayout::mono()},
			{2, AudioChannelLayout::stereo()},
			{6, AudioChannelLayout::surround51()},
			{8, AudioChannelLayout::surround71()},
			{6, AudioChannelLayout::discrete(6)},
		}};

		for (const auto& [channels, layout] : cases)
		{
			constexpr auto frames = f_cnt_t{7};
			constexpr auto sampleRate = int{44100};
			const auto original
				= SampleBuffer{makeInterleaved(frames, channels), channels, layout, sampleRate};

			const auto encoded = original.toBase64();
			QVERIFY(!encoded.isEmpty());

			const auto decoded
				= SampleBuffer::fromBase64(encoded, sampleRate, channels, layout.toString());
			QVERIFY(decoded != nullptr);
			QVERIFY(!decoded->empty());
			QCOMPARE(decoded->channels(), channels);
			QCOMPARE(decoded->frames(), frames);
			QCOMPARE(decoded->sampleRate(), static_cast<sample_rate_t>(sampleRate));
			QVERIFY(decoded->layout() == layout);

			for (f_cnt_t frame = 0; frame < frames; ++frame)
			{
				for (ch_cnt_t channel = 0; channel < channels; ++channel)
				{
					QVERIFY(decoded->sample(frame, channel) == original.sample(frame, channel));
				}
			}
		}
	}

	//! WAV export and import preserve channel count, order and known speaker roles
	void WaveFile_MultichannelRoundTrip()
	{
		auto directory = QTemporaryDir{};
		QVERIFY(directory.isValid());

		const auto cases = std::array<std::pair<ch_cnt_t, AudioChannelLayout>, 5>{{
			{1, AudioChannelLayout::mono()},
			{2, AudioChannelLayout::stereo()},
			{6, AudioChannelLayout::surround51()},
			{6, AudioChannelLayout::discrete(6)},
			{8, AudioChannelLayout::discrete(8)},
		}};

		auto index = 0;
		for (const auto& [channels, layout] : cases)
		{
			constexpr auto frames = f_cnt_t{9};
			constexpr auto sampleRate = int{48000};
			const auto original
				= SampleBuffer{makeInterleaved(frames, channels), channels, layout, sampleRate};
			const auto path = directory.filePath(QStringLiteral("take-%1.wav").arg(index++));

			QVERIFY(original.toWaveFile(path));
			const auto loaded = SampleBuffer::fromFile(path);
			QVERIFY(loaded != nullptr);
			QVERIFY(!loaded->empty());
			QCOMPARE(loaded->channels(), channels);
			QCOMPARE(loaded->layout().channels(), channels);
			QVERIFY(loaded->layout() == layout);
			QCOMPARE(loaded->frames(), frames);
			QCOMPARE(loaded->sampleRate(), static_cast<sample_rate_t>(sampleRate));

			for (f_cnt_t frame = 0; frame < frames; ++frame)
			{
				for (ch_cnt_t channel = 0; channel < channels; ++channel)
				{
					QVERIFY(loaded->sample(frame, channel) == original.sample(frame, channel));
				}
			}
		}
	}

	//! The legacy stereo constructors are unchanged
	void LegacyStereo_SampleFrameArray()
	{
		constexpr auto frames = size_t{4};
		auto source = std::vector<SampleFrame>(frames);
		for (size_t frame = 0; frame < frames; ++frame)
		{
			source[frame]
				= SampleFrame{static_cast<float>(frame) + 0.5f, -static_cast<float>(frame) - 0.25f};
		}

		const auto fromPointer = SampleBuffer{source.data(), frames, 48000};
		QCOMPARE(fromPointer.channels(), DEFAULT_CHANNELS);
		QCOMPARE(fromPointer.layout().channels(), DEFAULT_CHANNELS);
		QVERIFY(fromPointer.layout() == AudioChannelLayout::stereo());
		QCOMPARE(fromPointer.frames(), static_cast<f_cnt_t>(frames));
		QCOMPARE(fromPointer.sampleRate(), sample_rate_t{48000});
		QVERIFY(fromPointer.data() != nullptr);
		for (size_t frame = 0; frame < frames; ++frame)
		{
			QVERIFY(fromPointer.data()[frame].left() == source[frame].left());
			QVERIFY(fromPointer.data()[frame].right() == source[frame].right());
			QVERIFY(fromPointer.sample(frame, 0) == source[frame].left());
			QVERIFY(fromPointer.sample(frame, 1) == source[frame].right());
		}

		const auto fromVector = SampleBuffer{source, 44100, QStringLiteral("kick.wav")};
		QCOMPARE(fromVector.channels(), DEFAULT_CHANNELS);
		QVERIFY(fromVector.layout() == AudioChannelLayout::stereo());
		QCOMPARE(fromVector.frames(), static_cast<f_cnt_t>(frames));
		QCOMPARE(fromVector.sampleRate(), sample_rate_t{44100});
		QCOMPARE(fromVector.audioFile(), QStringLiteral("kick.wav"));
		for (size_t frame = 0; frame < frames; ++frame)
		{
			QVERIFY(fromVector.sample(frame, 0) == source[frame].left());
			QVERIFY(fromVector.sample(frame, 1) == source[frame].right());
		}
	}

	//! Base64 data that does not describe whole frames yields an empty buffer
	void Base64_InvalidSize_ReturnsEmptyBuffer()
	{
		// Three floats cannot be split into whole stereo frames
		const auto notWholeFrames
			= QByteArray(3 * static_cast<int>(sizeof(float)), '\0').toBase64();
		const auto decoded
			= SampleBuffer::fromBase64(QString::fromLatin1(notWholeFrames), 48000, ch_cnt_t{2});
		QVERIFY(decoded != nullptr);
		QVERIFY(decoded->empty());
		QCOMPARE(decoded->channels(), ch_cnt_t{0});
		QCOMPARE(decoded->frames(), f_cnt_t{0});

		QVERIFY(SampleBuffer::fromBase64(QString{}, 48000, ch_cnt_t{2})->empty());
		QVERIFY(SampleBuffer::fromBase64(QStringLiteral("AAAA"), 48000, ch_cnt_t{0})->empty());
		QVERIFY(SampleBuffer::emptyBuffer()->empty());
	}

	//! A channel count that disagrees with the layout cannot be stored silently
	void Construction_ChannelLayoutMismatch_Throws()
	{
		QVERIFY_EXCEPTION_THROWN(
			(SampleBuffer(std::vector<float>(8, 0.f), 2, AudioChannelLayout::mono(), 48000)),
			std::invalid_argument);
		QVERIFY_EXCEPTION_THROWN(
			(SampleBuffer(std::vector<float>(8, 0.f), 8, AudioChannelLayout::surround51(), 48000)),
			std::invalid_argument);
		// Sample count must be a whole number of frames
		QVERIFY_EXCEPTION_THROWN(
			(SampleBuffer(std::vector<float>(7, 0.f), 2, AudioChannelLayout::stereo(), 48000)),
			std::invalid_argument);

		const auto planar = makePlanar(f_cnt_t{4}, ch_cnt_t{2});
		const auto pointers = planarPointers(planar);
		QVERIFY_EXCEPTION_THROWN(
			(SampleBuffer::fromPlanar(PlanarBufferView<const float>{pointers.data(), 2, 4},
				AudioChannelLayout::mono(), 48000)),
			std::invalid_argument);
	}

	//! An empty layout description falls back to a discrete layout for the data
	void Construction_EmptyLayoutBecomesDiscrete()
	{
		const auto buffer
			= SampleBuffer{std::vector<float>(6, 0.f), 2, AudioChannelLayout{}, 48000};
		QCOMPARE(buffer.channels(), ch_cnt_t{2});
		QCOMPARE(buffer.frames(), f_cnt_t{3});
		QCOMPARE(buffer.layout().channels(), ch_cnt_t{2});
		QVERIFY(buffer.layout().isDiscrete());
	}

	//! 5.1 source -> 5.1 destination preserves channel identity
	void MultichannelPlay_51To51_PreservesChannelIdentity()
	{
		constexpr auto frames = f_cnt_t{10};
		constexpr auto channels = ch_cnt_t{6};
		const auto layout = AudioChannelLayout::surround51();
		const auto sampleRate = Engine::audioEngine()->outputSampleRate();

		const auto sourceData = makeInterleaved(frames, channels);
		const auto buffer = std::make_shared<SampleBuffer>(sourceData, channels, layout, sampleRate);
		const auto sample = Sample{buffer};

		auto state = Sample::PlaybackState{AudioResampler::Mode::Linear, channels};

		auto planarStorage = std::vector<std::vector<float>>(channels, std::vector<float>(frames, 0.0f));
		auto planarPtrs = std::vector<float*>(channels);
		for (ch_cnt_t ch = 0; ch < channels; ++ch)
		{
			planarPtrs[ch] = planarStorage[ch].data();
		}
		auto dst = PlanarBufferView<float>{planarPtrs.data(), channels, frames};

		const bool result = sample.play(dst, layout, &state, frames);
		QVERIFY(result);

		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			for (ch_cnt_t ch = 0; ch < channels; ++ch)
			{
				QCOMPARE(dst.sample(ch, frame), sampleValue(frame, ch));
			}
		}
	}

	//! 5.1 source -> stereo matches the documented downmix
	void MultichannelPlay_51ToStereo_MatchesDocumentedDownmix()
	{
		constexpr auto frames = f_cnt_t{10};
		constexpr auto channels = ch_cnt_t{6};
		const auto layout51 = AudioChannelLayout::surround51();
		const auto layoutStereo = AudioChannelLayout::stereo();
		const auto sampleRate = Engine::audioEngine()->outputSampleRate();

		const auto sourceData = makeInterleaved(frames, channels);
		const auto buffer = std::make_shared<SampleBuffer>(sourceData, channels, layout51, sampleRate);
		const auto sample = Sample{buffer};

		auto state = Sample::PlaybackState{AudioResampler::Mode::Linear, channels};

		auto planarStorage = std::vector<std::vector<float>>(2, std::vector<float>(frames, 0.0f));
		auto planarPtrs = std::vector<float*>{planarStorage[0].data(), planarStorage[1].data()};
		auto dst = PlanarBufferView<float>{planarPtrs.data(), 2, frames};

		const bool result = sample.play(dst, layoutStereo, &state, frames);
		QVERIFY(result);

		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			float expectedLeft = 0.0f;
			float expectedRight = 0.0f;
			downmixFrameToStereo(sourceData.data() + frame * channels, layout51, expectedLeft, expectedRight);
			QVERIFY(qFuzzyCompare(dst.sample(0, frame), expectedLeft));
			QVERIFY(qFuzzyCompare(dst.sample(1, frame), expectedRight));
		}
	}
};

QTEST_GUILESS_MAIN(SampleBufferTest)
#include "SampleBufferTest.moc"
