/*
 * PerTrackRecordingTest.cpp - assigned source isolation for recording
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

#include "SampleRecordHandle.h"

#include <QtTest>

#include <array>
#include <memory>
#include <vector>

#include "AudioEngine.h"
#include "Engine.h"
#include "SampleCapture.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"

using namespace mxm;

class PerTrackRecordingTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase() { Engine::init(true); }
	void cleanupTestCase() { Engine::destroy(); }

	//! Two tracks reading different channels record only their assigned signal.
	void Recording_UsesAssignedSourceForCaptureAndMonitoring()
	{
		auto* engine = Engine::audioEngine();
		QVERIFY(engine != nullptr);
		const auto guard = engine->requestChangesGuard();

		const auto frames = engine->framesPerPeriod();
		constexpr auto channels = ch_cnt_t{8};
		engine->setInputChannels(channels);

		auto left = std::vector<float>(frames, 0.125f);
		auto right = std::vector<float>(frames, -0.25f);
		// Fill all other device channels with values that must not leak into
		// either track's take.
		auto unused = std::vector<std::vector<float>>(6, std::vector<float>(frames, 0.75f));
		auto pointers = std::vector<const float*>(channels);
		pointers[0] = unused[0].data();
		pointers[1] = unused[1].data();
		pointers[2] = left.data();
		pointers[3] = right.data();
		pointers[4] = unused[2].data();
		pointers[5] = unused[3].data();
		pointers[6] = unused[4].data();
		pointers[7] = unused[5].data();

		engine->pushInputFrames(PlanarBufferView<const float>{pointers.data(), channels, frames});
		engine->renderNextPeriod();

		auto song = Engine::getSong();
		auto track = SampleTrack{song};
		track.setInputMonitoring(true);
		auto clip = SampleClip{&track};

		auto session = std::make_shared<SampleRecordSession>();
		session->source = AudioInputSource{
			QStringLiteral("device:2+3"),
			QStringLiteral("Inputs 3 + 4"),
			2,
			AudioChannelLayout::stereo(),
			{2, 3},
			false,
		};
		session->destinationChannels = {0, 1};
		session->capture = std::make_shared<SampleCapture>(
			engine->inputSampleRate(), AudioChannelLayout::stereo(), frames, 8);

		auto handle = SampleRecordHandle{&clip, session};
		auto monitor = std::vector<SampleFrame>(frames);
		handle.play(monitor.data());

		QCOMPARE(track.takeInputLevel(), 0.25f);
		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			QVERIFY(monitor[frame][0] == 0.125f);
			QVERIFY(monitor[frame][1] == -0.25f);
		}

		const auto result = session->capture->finish();
		QVERIFY(result != nullptr);
		QVERIFY(!result->empty());
		QCOMPARE(result->channels(), ch_cnt_t{2});
		QCOMPARE(result->frames(), frames);
		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			QCOMPARE(result->sample(frame, 0), 0.125f);
			QCOMPARE(result->sample(frame, 1), -0.25f);
		}

		// A second track reads a different pair from the same capture snapshot
		// during the same period.
		auto otherTrack = SampleTrack{song};
		otherTrack.setInputMonitoring(true);
		auto otherClip = SampleClip{&otherTrack};
		auto otherSession = std::make_shared<SampleRecordSession>();
		otherSession->source = AudioInputSource{
			QStringLiteral("device:0+1"),
			QStringLiteral("Inputs 1 + 2"),
			2,
			AudioChannelLayout::stereo(),
			{0, 1},
			false,
		};
		otherSession->destinationChannels = {0, 1};
		otherSession->capture = std::make_shared<SampleCapture>(
			engine->inputSampleRate(), AudioChannelLayout::stereo(), frames, 8);

		auto otherHandle = SampleRecordHandle{&otherClip, otherSession};
		auto otherMonitor = std::vector<SampleFrame>(frames);
		otherHandle.play(otherMonitor.data());

		QCOMPARE(otherTrack.takeInputLevel(), 0.75f);
		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			QVERIFY(otherMonitor[frame][0] == 0.75f);
			QVERIFY(otherMonitor[frame][1] == 0.75f);
		}
		const auto otherResult = otherSession->capture->finish();
		QVERIFY(otherResult != nullptr);
		QVERIFY(!otherResult->empty());
		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			QCOMPARE(otherResult->sample(frame, 0), 0.75f);
			QCOMPARE(otherResult->sample(frame, 1), 0.75f);
		}

		engine->setInputChannels(0);
	}

	//! A saved map that no longer fits the device is reported, not remapped.
	void Recording_UnavailableSourceIsNotSubstituted()
	{
		auto* engine = Engine::audioEngine();
		QVERIFY(engine != nullptr);
		const auto guard = engine->requestChangesGuard();
		engine->setInputChannels(2);
		auto left = std::vector<float>(engine->framesPerPeriod(), 0.5f);
		auto right = std::vector<float>(engine->framesPerPeriod(), 0.5f);
		const auto pointers = std::array<const float*, 2>{left.data(), right.data()};
		engine->pushInputFrames(
			PlanarBufferView<const float>{pointers.data(), ch_cnt_t{2}, engine->framesPerPeriod()});
		engine->renderNextPeriod();

		auto song = Engine::getSong();
		auto track = SampleTrack{song};
		auto clip = SampleClip{&track};

		auto session = std::make_shared<SampleRecordSession>();
		session->source = AudioInputSource{
			QStringLiteral("device:7"),
			QStringLiteral("Input 8"),
			1,
			AudioChannelLayout::mono(),
			{7},
			false,
		};
		session->destinationChannels = {0};
		session->capture = std::make_shared<SampleCapture>(
			engine->inputSampleRate(), AudioChannelLayout::mono(),
			engine->framesPerPeriod(), 8);

		auto handle = SampleRecordHandle{&clip, session};
		auto monitor = std::vector<SampleFrame>(engine->framesPerPeriod());
		handle.play(monitor.data());

		QVERIFY(session->inputUnavailable.load());
		QCOMPARE(session->capture->recordedFrames(), std::uint64_t{0});

		engine->setInputChannels(0);
	}
};

QTEST_GUILESS_MAIN(PerTrackRecordingTest)
#include "PerTrackRecordingTest.moc"
