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
#include <array>
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

	void Recording_EightTracksKeepMonoInputsIsolated()
	{
		auto* engine = Engine::audioEngine();
		QVERIFY(engine != nullptr);
		const auto guard = engine->requestChangesGuard();
		constexpr auto trackCount = ch_cnt_t{8};
		const auto frames = engine->framesPerPeriod();
		const auto song = Engine::getSong();
		auto tracks = std::array<std::unique_ptr<SampleTrack>, 8>{};
		auto clips = std::array<std::unique_ptr<SampleClip>, 8>{};
		auto sessions = std::array<std::shared_ptr<SampleRecordSession>, 8>{};
		auto input = std::array<std::vector<float>, 8>{};
		auto pointers = std::array<const float*, 8>{};
		const auto firstId = quint64{0x5200};

		for (ch_cnt_t channel = 0; channel < trackCount; ++channel)
		{
			input[channel].assign(frames, static_cast<float>(channel + 1) / 10.f);
			pointers[channel] = input[channel].data();
			engine->registerTrackInput(firstId + channel,
				QStringLiteral("Mono input %1").arg(channel + 1), 1);
			engine->pushTrackInputFrames(firstId + channel,
				PlanarBufferView<const float>{&pointers[channel], 1, frames});

			tracks[channel] = std::make_unique<SampleTrack>(song);
			clips[channel] = std::make_unique<SampleClip>(tracks[channel].get());
			sessions[channel] = std::make_shared<SampleRecordSession>();
			sessions[channel]->source = makeAudioInputSource(
				QStringLiteral("input:%1").arg(channel), QStringLiteral("Input %1").arg(channel + 1),
				1, {QStringLiteral("input:%1").arg(channel)});
			sessions[channel]->trackInputId = firstId + channel;
			sessions[channel]->capture = std::make_shared<SampleCapture>(
				engine->outputSampleRate(), AudioChannelLayout::mono(), frames, 8);
		}

		engine->renderNextPeriod();
		auto handles = std::array<std::unique_ptr<SampleRecordHandle>, 8>{};
		for (ch_cnt_t channel = 0; channel < trackCount; ++channel)
		{
			handles[channel] = std::make_unique<SampleRecordHandle>(clips[channel].get(), sessions[channel]);
			handles[channel]->play(nullptr);
			QCOMPARE(sessions[channel]->recordedFrames.load(), static_cast<std::uint64_t>(frames));
			const auto result = sessions[channel]->capture->finish();
			QVERIFY(result && !result->empty());
			QCOMPARE(result->channels(), ch_cnt_t{1});
			for (f_cnt_t frame = 0; frame < frames; ++frame)
			{
				QCOMPARE(result->sample(frame, 0), input[channel][frame]);
			}
			engine->unregisterTrackInput(firstId + channel);
		}
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
