/*
 * AudioEngineTrackInputTest.cpp - backend track input port bridge
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

#include "AudioEngine.h"

#include <QtTest>

#include <array>
#include <vector>

#include "Engine.h"

using namespace mxm;

class AudioEngineTrackInputTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase() { Engine::init(true); }
	void cleanupTestCase() { Engine::destroy(); }

	//! Registering a backend track input exposes a resolvable source and drains audio.
	void TrackInput_RegisterPushDrainAndRemove()
	{
		auto* engine = Engine::audioEngine();
		QVERIFY(engine != nullptr);
		constexpr auto id = quint64{0x51a1};

		// Keep the background render thread out of the way while the per-track
		// queue is filled and read back deterministically.
		const auto guard = engine->requestChangesGuard();

		engine->registerTrackInput(id, QStringLiteral("Guitar"), 2);
		QVERIFY(engine->hasTrackInput(id));

		// The synthetic external-patchbay source is not a device endpoint. The
		// dummy backend cannot route one, so it must not be offered.
		QVERIFY(engine->audioDev() != nullptr);
		QVERIFY(!engine->audioDev()->supportsExternalPatchbayInput());
		QVERIFY(!engine->trackInputSource(id, QStringLiteral("Guitar")).has_value());

		constexpr auto frames = f_cnt_t{16};
		auto left = std::vector<float>(frames);
		auto right = std::vector<float>(frames);
		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			left[frame] = static_cast<float>(frame) + 0.25f;
			right[frame] = -static_cast<float>(frame) - 0.5f;
		}
		const auto pointers = std::array<const float*, 2>{left.data(), right.data()};

		engine->pushTrackInputFrames(id, {pointers.data(), 2, frames});
		engine->renderNextPeriod();

		const auto captured = engine->trackInputBuffer(id);
		QCOMPARE(captured.channels(), ch_cnt_t{2});
		QCOMPARE(captured.frames(), frames);
		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			QVERIFY(captured.bufferPtr(0)[frame] == left[frame]);
			QVERIFY(captured.bufferPtr(1)[frame] == right[frame]);
		}

		engine->unregisterTrackInput(id);
		QVERIFY(!engine->hasTrackInput(id));
		QVERIFY(engine->trackInputBuffer(id).empty());
		QVERIFY(!engine->trackInputSource(id, QStringLiteral("Guitar")).has_value());
	}

	void TrackInput_PreservesEightChannelsAndRejectsMismatches()
	{
		auto* engine = Engine::audioEngine();
		QVERIFY(engine != nullptr);
		constexpr auto id = quint64{0x51a2};
		constexpr auto frames = f_cnt_t{12};
		const auto guard = engine->requestChangesGuard();
		engine->registerTrackInput(id, QStringLiteral("Multichannel"), 8);

		auto channels = std::array<std::vector<float>, 8>{};
		auto pointers = std::array<const float*, 8>{};
		for (ch_cnt_t channel = 0; channel < 8; ++channel)
		{
			channels[channel].resize(frames);
			for (f_cnt_t frame = 0; frame < frames; ++frame)
			{
				channels[channel][frame] = static_cast<float>(channel * 100 + frame);
			}
			pointers[channel] = channels[channel].data();
		}

		engine->pushTrackInputFrames(id, {pointers.data(), 8, frames});
		engine->renderNextPeriod();
		auto captured = engine->trackInputBuffer(id);
		QCOMPARE(captured.channels(), ch_cnt_t{8});
		QCOMPARE(captured.frames(), frames);
		for (ch_cnt_t channel = 0; channel < 8; ++channel)
		{
			for (f_cnt_t frame = 0; frame < frames; ++frame)
			{
				QCOMPARE(captured.bufferPtr(channel)[frame], channels[channel][frame]);
			}
		}

		auto interleaved = std::vector<float>(static_cast<std::size_t>(frames) * 8);
		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			for (ch_cnt_t channel = 0; channel < 8; ++channel)
			{
				interleaved[static_cast<std::size_t>(frame) * 8 + channel]
					= -channels[channel][frame];
			}
		}
		engine->pushTrackInputFrames(id, {interleaved.data(), 8, frames});
		engine->renderNextPeriod();
		captured = engine->trackInputBuffer(id);
		QCOMPARE(captured.channels(), ch_cnt_t{8});
		QCOMPARE(captured.frames(), frames);
		for (ch_cnt_t channel = 0; channel < 8; ++channel)
		{
			for (f_cnt_t frame = 0; frame < frames; ++frame)
			{
				QCOMPARE(captured.bufferPtr(channel)[frame], -channels[channel][frame]);
			}
		}

		const auto droppedBefore = engine->trackInputDroppedFrames(id);
		const auto mono = std::vector<float>(frames, 0.5f);
		const auto monoPointer = std::array<const float*, 1>{mono.data()};
		engine->pushTrackInputFrames(id, {monoPointer.data(), 1, frames});
		engine->pushTrackInputFrames(id, InterleavedBufferView<const float>{mono.data(), 1, frames});
		QCOMPARE(engine->trackInputDroppedFrames(id), droppedBefore + 2 * frames);
		engine->renderNextPeriod();
		QVERIFY(engine->trackInputBuffer(id).empty());

		engine->unregisterTrackInput(id);
	}
};

QTEST_GUILESS_MAIN(AudioEngineTrackInputTest)
#include "AudioEngineTrackInputTest.moc"
