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

		const auto source = engine->trackInputSource(id, QStringLiteral("Guitar"));
		QVERIFY(source.has_value());
		QCOMPARE(source->id, QStringLiteral("track"));
		QVERIFY(source->trackPort);
		QCOMPARE(source->channels, ch_cnt_t{2});
		QCOMPARE(source->captureChannels.size(), std::size_t{2});

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
};

QTEST_GUILESS_MAIN(AudioEngineTrackInputTest)
#include "AudioEngineTrackInputTest.moc"
