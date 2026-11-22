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

	//! A per-track backend stream reaches the recording writer.
	void Recording_UsesPerTrackCaptureBuffer()
	{
		auto* engine = Engine::audioEngine();
		QVERIFY(engine != nullptr);
		const auto guard = engine->requestChangesGuard();
		constexpr auto trackInputId = quint64{0x51a3};
		const auto frames = engine->framesPerPeriod();
		const auto left = std::vector<float>(frames, 0.25f);
		const auto right = std::vector<float>(frames, -0.5f);
		const auto pointers = std::array<const float*, 2>{left.data(), right.data()};
		engine->registerTrackInput(trackInputId, QStringLiteral("Track input"), 2);
		engine->pushTrackInputFrames(trackInputId, {pointers.data(), 2, frames});
		engine->renderNextPeriod();

		auto song = Engine::getSong();
		auto track = SampleTrack{song};
		auto clip = SampleClip{&track};
		auto session = std::make_shared<SampleRecordSession>();
		session->source = makeAudioInputSource(
			QStringLiteral("sdl:headset"), QStringLiteral("Headset microphone"),
			2, {QStringLiteral("Headset microphone")});
		session->trackInputId = trackInputId;
		session->destinationChannels = {0, 1};
		session->capture = std::make_shared<SampleCapture>(
			engine->outputSampleRate(), AudioChannelLayout::stereo(), frames, 8);
		auto handle = SampleRecordHandle{&clip, session};
		handle.play(nullptr);

		QCOMPARE(session->recordedFrames.load(), static_cast<std::uint64_t>(frames));
		const auto result = session->capture->finish();
		QVERIFY(result && !result->empty());
		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			QCOMPARE(result->sample(frame, 0), 0.25f);
			QCOMPARE(result->sample(frame, 1), -0.5f);
		}
		engine->unregisterTrackInput(trackInputId);
	}

	//! An empty period is not a failed capture. Only a reported open/reader failure is.
	void Recording_EmptySnapshotIsNotUnavailable()
	{
		auto* engine = Engine::audioEngine();
		const auto guard = engine->requestChangesGuard();
		constexpr auto trackInputId = quint64{0x51a4};
		engine->registerTrackInput(trackInputId, QStringLiteral("Track input"), 2);
		engine->renderNextPeriod();

		auto track = SampleTrack{Engine::getSong()};
		auto clip = SampleClip{&track};
		auto session = std::make_shared<SampleRecordSession>();
		session->source = makeAudioInputSource(
			QStringLiteral("sdl:headset"), QStringLiteral("Headset microphone"),
			2, {QStringLiteral("Headset microphone")});
		session->trackInputId = trackInputId;
		session->destinationChannels = {0, 1};
		session->capture = std::make_shared<SampleCapture>(
			engine->outputSampleRate(), AudioChannelLayout::stereo(), engine->framesPerPeriod(), 8);
		auto handle = SampleRecordHandle{&clip, session};
		handle.play(nullptr);

		QVERIFY(!session->inputUnavailable.load());
		QCOMPARE(session->recordedFrames.load(), static_cast<std::uint64_t>(0));
		engine->unregisterTrackInput(trackInputId);
	}

	void Recording_FailedCaptureSetsInputUnavailable()
	{
		auto* engine = Engine::audioEngine();
		const auto guard = engine->requestChangesGuard();
		constexpr auto trackInputId = quint64{0x51a5};
		engine->registerTrackInput(trackInputId, QStringLiteral("Track input"), 2);
		engine->setTrackInputCaptureFailed(trackInputId, true);
		QVERIFY(engine->trackInputCaptureFailed(trackInputId));

		auto track = SampleTrack{Engine::getSong()};
		auto clip = SampleClip{&track};
		auto session = std::make_shared<SampleRecordSession>();
		session->source = makeAudioInputSource(
			QStringLiteral("sdl:headset"), QStringLiteral("Headset microphone"),
			2, {QStringLiteral("Headset microphone")});
		session->trackInputId = trackInputId;
		session->destinationChannels = {0, 1};
		session->capture = std::make_shared<SampleCapture>(
			engine->outputSampleRate(), AudioChannelLayout::stereo(), engine->framesPerPeriod(), 8);
		auto handle = SampleRecordHandle{&clip, session};
		handle.play(nullptr);

		QVERIFY(session->inputUnavailable.load());
		QCOMPARE(session->recordedFrames.load(), static_cast<std::uint64_t>(0));
		engine->setTrackInputCaptureFailed(trackInputId, false);
		QVERIFY(!engine->trackInputCaptureFailed(trackInputId));
		engine->unregisterTrackInput(trackInputId);
	}
};

QTEST_GUILESS_MAIN(PerTrackRecordingTest)
#include "PerTrackRecordingTest.moc"
