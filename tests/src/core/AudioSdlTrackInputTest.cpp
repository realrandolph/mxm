#include "AudioSdl.h"

#include <QtTest>

#include <algorithm>

#include "AudioEngine.h"
#include "Engine.h"

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
				return source.id.startsWith(QStringLiteral("sdl:"))
					&& !source.displayName.contains(QStringLiteral("Input "))
					&& source.trackPort && source.endpointPorts.size() == 1;
			}));

			constexpr auto trackId = quint64{0x51a2};
			engine->registerTrackInput(trackId, QStringLiteral("SDL input test"), 2);
			QVERIFY(backend->registerTrackInputPort(trackId, QStringLiteral("SDL input test"), 2));
			backend->setTrackInputSource(trackId, sources.front().endpointPorts);
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
};

QTEST_GUILESS_MAIN(AudioSdlTrackInputTest)
#include "AudioSdlTrackInputTest.moc"
