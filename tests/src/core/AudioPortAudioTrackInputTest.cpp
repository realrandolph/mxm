#include "AudioPortAudio.h"

#include <QtTest>

#include <QSet>
#include <algorithm>

#include "AudioEngine.h"
#include "Engine.h"

using namespace mxm;

class AudioPortAudioTrackInputTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase() { Engine::init(true); }
	void cleanupTestCase() { Engine::destroy(); }

	void InputDevicesAreStableTrackSources()
	{
		auto* engine = Engine::audioEngine();
		QVERIFY(engine != nullptr);
		QVERIFY(engine->audioDev() != nullptr);

		const auto initError = Pa_Initialize();
		if (initError != paNoError)
		{
			QSKIP(qPrintable(QStringLiteral("PortAudio unavailable: %1")
				.arg(Pa_GetErrorText(initError))));
		}

		auto haveInput = false;
		for (auto i = 0, deviceCount = Pa_GetDeviceCount(); i < deviceCount; ++i)
		{
			const auto* info = Pa_GetDeviceInfo(i);
			if (info && info->maxInputChannels > 0) { haveInput = true; }
		}
		Pa_Terminate();
		if (!haveInput) { QSKIP("No PortAudio input devices"); }

		engine->audioDev()->stopProcessing();

		auto opened = false;
		auto device = AudioPortAudio{opened, engine};
		if (!opened) { QSKIP("No usable PortAudio output device"); }

		auto* backend = static_cast<AudioDevice*>(&device);
		QVERIFY(backend->supportsTrackInputPorts());

		const auto first = backend->inputSources();
		QVERIFY(!first.empty());

		auto ids = QSet<QString>{};
		auto labels = QSet<QString>{};
		for (const auto& source : first)
		{
			QVERIFY(source.valid());
			QVERIFY(source.id == QString::fromLatin1(SystemDefaultInputSourceId)
				|| source.id.startsWith(QStringLiteral("portaudio:")));
			QVERIFY(source.channels == 1 || source.channels == 2);
			QVERIFY(source.endpointPorts == QStringList{source.id});
			QVERIFY(!source.displayName.isEmpty());
			QVERIFY(!ids.contains(source.id));
			QVERIFY(!labels.contains(source.displayName));
			ids.insert(source.id);
			labels.insert(source.displayName);
		}

		const auto second = backend->inputSources();
		QCOMPARE(second.size(), first.size());
		for (auto i = std::size_t{0}; i < first.size(); ++i)
		{
			QCOMPARE(second[i].id, first[i].id);
			QCOMPARE(second[i].displayName, first[i].displayName);
		}

		constexpr auto trackId = quint64{0x504f5254};
		engine->registerTrackInput(trackId, QStringLiteral("PortAudio input test"), 2);
		QVERIFY(!backend->registerTrackInputPort(trackId, QStringLiteral("PortAudio input test"), 1));
		QVERIFY(backend->registerTrackInputPort(trackId, QStringLiteral("PortAudio input test"), 2));
		QVERIFY(!backend->supportsExternalPatchbayInput());
		backend->setTrackInputSource(trackId, {QStringLiteral("portaudio:missing")});
		QVERIFY(engine->trackInputCaptureFailed(trackId));
		backend->setTrackInputSource(trackId, first.front().endpointPorts);
		QVERIFY(!engine->trackInputCaptureFailed(trackId));
		backend->startProcessing();

		QElapsedTimer timer;
		timer.start();
		while (timer.elapsed() < 3000 && engine->trackInputBuffer(trackId).empty())
		{
			QTest::qWait(10);
		}
		backend->stopProcessing();

		const auto captured = engine->trackInputBuffer(trackId);
		backend->unregisterTrackInputPort(trackId);
		engine->unregisterTrackInput(trackId);

		if (captured.empty()) { QSKIP("PortAudio input stream did not deliver frames"); }
		QCOMPARE(captured.channels(), ch_cnt_t{2});
	}
};

QTEST_GUILESS_MAIN(AudioPortAudioTrackInputTest)
#include "AudioPortAudioTrackInputTest.moc"
