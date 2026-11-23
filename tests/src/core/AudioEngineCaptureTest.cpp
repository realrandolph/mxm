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

#include "Engine.h"

using namespace mxm;

class AudioEngineCaptureTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase() { Engine::init(true); }

	void cleanupTestCase() { Engine::destroy(); }

	//! Backend device-change events are forwarded through the shared engine signal
	void AudioDeviceChange_ForwardedToUi()
	{
		auto* engine = Engine::audioEngine();
		QVERIFY(engine != nullptr);
		QSignalSpy spy(engine, &AudioEngine::audioDeviceChanged);
		engine->notifyAudioDeviceChanged(true, QStringLiteral("Headset"), QString{}, false);
		QCOMPARE(spy.count(), 1);
		const auto arguments = spy.takeFirst();
		QCOMPARE(arguments[0].toBool(), true);
		QCOMPARE(arguments[1].toString(), QStringLiteral("Headset"));
		QCOMPARE(arguments[2].toString(), QString{});
		QCOMPARE(arguments[3].toBool(), false);
	}

	//! Output device changes and route switches are also forwarded through the shared signal
	void AudioDeviceChange_OutputRouteAndAvailability()
	{
		auto* engine = Engine::audioEngine();
		QVERIFY(engine != nullptr);
		QSignalSpy spy(engine, &AudioEngine::audioDeviceChanged);

		// Output unavailable
		engine->notifyAudioDeviceChanged(false, QStringLiteral("Speakers"), QString{}, false);
		QCOMPARE(spy.count(), 1);
		auto args = spy.takeFirst();
		QCOMPARE(args[0].toBool(), false);
		QCOMPARE(args[1].toString(), QStringLiteral("Speakers"));
		QCOMPARE(args[2].toString(), QString{});
		QCOMPARE(args[3].toBool(), false);

		// Route changed
		engine->notifyAudioDeviceChanged(false, QStringLiteral("Speakers"), QStringLiteral("Headphones"), true);
		QCOMPARE(spy.count(), 1);
		args = spy.takeFirst();
		QCOMPARE(args[0].toBool(), false);
		QCOMPARE(args[1].toString(), QStringLiteral("Speakers"));
		QCOMPARE(args[2].toString(), QStringLiteral("Headphones"));
		QCOMPARE(args[3].toBool(), true);
	}

};

QTEST_GUILESS_MAIN(AudioEngineCaptureTest)
#include "AudioEngineCaptureTest.moc"
