#include "AudioSdl.h"

#include <QtTest>

#include <algorithm>

#include "AudioEngine.h"
#include "Engine.h"
#include "SampleTrack.h"
#include "Song.h"

using namespace mxm;

class AudioSdlTrackInputTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase() { Engine::init(true); }
	void cleanupTestCase() { Engine::destroy(); }

	void CaptureDevicesAreTrackSources()
	{
		auto* engine = Engine::audioEngine();
		QVERIFY(engine != nullptr);
		engine->audioDev()->stopProcessing();
		bool opened = false;
		{
			AudioSdl sdl{opened, engine};
			QVERIFY(opened);
			auto* backend = static_cast<AudioDevice*>(&sdl);
			const auto sources = backend->inputSources();
			QVERIFY(!sources.empty());
			QVERIFY(std::ranges::all_of(sources, [](const auto& source) {
				return (source.id == QString::fromLatin1(SystemDefaultInputSourceId)
					|| source.id.startsWith(QStringLiteral("sdl:")))
					&& !source.displayName.contains(QStringLiteral("Input "))
					&& source.endpointPorts.size() == 1;
			}));

			constexpr auto trackId = quint64{0x51a2};
			engine->registerTrackInput(trackId, QStringLiteral("SDL input test"), 2);
			QVERIFY(backend->registerTrackInputPort(trackId, QStringLiteral("SDL input test"), 2));
			QVERIFY(!backend->supportsExternalPatchbayInput());
			backend->setTrackInputSource(trackId, {QStringLiteral("mxm-no-such-sdl-device")});
			QVERIFY(engine->trackInputCaptureFailed(trackId));

			backend->setTrackInputSource(trackId, sources.front().endpointPorts);
			QVERIFY(!engine->trackInputCaptureFailed(trackId));
			backend->startProcessing();

			QElapsedTimer timer;
			timer.start();
			while (timer.elapsed() < 3000 && engine->trackInputBuffer(trackId).empty())
			{
				QTest::qWait(10);
			}
			backend->stopProcessing();
			QVERIFY(!engine->trackInputBuffer(trackId).empty());
			backend->unregisterTrackInputPort(trackId);
			engine->unregisterTrackInput(trackId);
		}
	}

	//! The engine must not treat the synthetic patchbay id as a device disconnect.
	void ExternalPatchbayAssignmentDoesNotCloseCapture()
	{
		auto* engine = Engine::audioEngine();
		QVERIFY(engine != nullptr);
		engine->storeAudioDevice();
		bool opened = false;
		auto* sdl = new AudioSdl(opened, engine);
		if (!opened)
		{
			delete sdl;
			engine->restoreAudioDevice();
			QFAIL("SDL capture backend did not open");
		}
		engine->setAudioDevice(sdl, false);
		struct RestoreDevice
		{
			AudioEngine* engine;
			~RestoreDevice() { engine->restoreAudioDevice(); }
		} restore{engine};

		constexpr auto trackId = quint64{0x51a7};
		engine->registerTrackInput(trackId, QStringLiteral("SDL patchbay guard"), 2);
		QVERIFY(engine->hasTrackInput(trackId));
		QVERIFY(!sdl->supportsExternalPatchbayInput());
		QVERIFY(!engine->trackInputSource(trackId, QStringLiteral("External patchbay input")).has_value());

		const auto sources = engine->inputSources();
		QVERIFY(!sources.empty());
		engine->setTrackInputSource(trackId, sources.front().id);
		QVERIFY(!engine->trackInputCaptureFailed(trackId));
		sdl->startProcessing();

		QElapsedTimer timer;
		timer.start();
		while (timer.elapsed() < 3000 && engine->trackInputBuffer(trackId).empty())
		{
			engine->renderNextPeriod();
			QTest::qWait(10);
		}
		QVERIFY(!engine->trackInputBuffer(trackId).empty());

		engine->setTrackInputSource(trackId, QString::fromLatin1(TrackInputSourceId));
		// Drop frames queued before the assignment so a closed stream cannot pass.
		engine->renderNextPeriod();
		QTest::qWait(30);
		engine->renderNextPeriod();
		auto stillCapturing = false;
		timer.restart();
		while (timer.elapsed() < 2000)
		{
			engine->renderNextPeriod();
			if (!engine->trackInputBuffer(trackId).empty())
			{
				stillCapturing = true;
				break;
			}
			QTest::qWait(10);
		}
		engine->unregisterTrackInput(trackId);
		QVERIFY(stillCapturing);

		auto* song = Engine::getSong();
		auto track = SampleTrack{song};
		const auto trackInputId = static_cast<quint64>(reinterpret_cast<std::uintptr_t>(&track));
		QVERIFY(engine->hasTrackInput(trackInputId));
		track.setInputAssignment(QString::fromLatin1(SystemDefaultInputSourceId));
		engine->setTrackInputCaptureFailed(trackInputId, true);
		QVERIFY(track.inputAssignmentStatus().contains(QStringLiteral("could not be opened")));
		song->clearErrors();
		QVERIFY(!track.startRecording());
		QVERIFY(song->errorSummary().contains(QStringLiteral("could not be opened")));
		QVERIFY(!track.isRecording());
	}
};

QTEST_GUILESS_MAIN(AudioSdlTrackInputTest)
#include "AudioSdlTrackInputTest.moc"
