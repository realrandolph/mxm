/*
 * MixerLayoutTest.cpp - channel layout routing through the mixer
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

#include "Mixer.h"

#include <QDomDocument>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>
#include <vector>

#include "AudioChannelBuffer.h"
#include "AudioEngine.h"
#include "AudioFileWave.h"
#include "Engine.h"
#include "OutputSettings.h"

#include <sndfile.h>

using namespace mxm;

namespace
{

auto sampleValue(ch_cnt_t channel, f_cnt_t frame) -> float
{
	return static_cast<float>(channel + 1) * 0.1f + static_cast<float>(frame) * 0.001f;
}

auto isClose(float actual, float expected) -> bool
{
	return std::abs(actual - expected) <= 1e-5f;
}

} // namespace

class MixerLayoutTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		QVERIFY(Engine::mixer() != nullptr);
		QVERIFY(Engine::audioEngine() != nullptr);
	}

	void cleanupTestCase() { Engine::destroy(); }

	void init()
	{
		const auto guard = Engine::audioEngine()->requestChangesGuard();
		Engine::mixer()->mixerChannel(0)->setChannelLayout(AudioChannelLayout::stereo());
		Engine::mixer()->mixerChannel(0)->m_buffer->silenceAllChannels();
	}

	//! Changing the master layout reallocates the engine's render buffers.
	void MasterLayout_PropagatesToEngine()
	{
		const auto guard = Engine::audioEngine()->requestChangesGuard();
		auto* master = Engine::mixer()->mixerChannel(0);
		master->setChannelLayout(AudioChannelLayout::surround51());

		QCOMPARE(master->channelLayout().channels(), ch_cnt_t{6});
		QCOMPARE(Engine::audioEngine()->masterLayout().channels(), ch_cnt_t{6});
		QCOMPARE(Engine::audioEngine()->renderedMasterBuffer().channels(), ch_cnt_t{6});
	}

	//! A 5.1 bus feeding a 5.1 mixer channel preserves every channel.
	void MixToChannel_PreservesSurroundChannels()
	{
		const auto guard = Engine::audioEngine()->requestChangesGuard();
		constexpr auto frames = f_cnt_t{64};
		auto* mixer = Engine::mixer();
		auto* master = mixer->mixerChannel(0);
		master->setChannelLayout(AudioChannelLayout::surround51());
		master->m_buffer->silenceAllChannels();

		auto source = AudioBuffer{frames, 6};
		for (ch_cnt_t channel = 0; channel < 6; ++channel)
		{
			for (f_cnt_t frame = 0; frame < frames; ++frame)
			{
				source.buffer(channel)[frame] = sampleValue(channel, frame);
			}
		}

		mixer->mixToChannel(source, AudioChannelLayout::surround51(), 0);

		const auto output = logicalBuffers(*master->m_buffer, master->channelLayout());
		QCOMPARE(output.channels(), ch_cnt_t{6});
		for (ch_cnt_t channel = 0; channel < 6; ++channel)
		{
			for (f_cnt_t frame = 0; frame < frames; ++frame)
			{
				QVERIFY2(isClose(output.bufferPtr(channel)[frame], sampleValue(channel, frame)),
					qPrintable(QStringLiteral("channel %1 frame %2").arg(channel).arg(frame)));
			}
		}
	}

	//! A surround bus feeding a stereo mixer channel uses the documented fold.
	void MixToChannel_DownmixesSurroundToStereo()
	{
		const auto guard = Engine::audioEngine()->requestChangesGuard();
		constexpr auto frames = f_cnt_t{8};
		auto* mixer = Engine::mixer();
		auto* master = mixer->mixerChannel(0);
		master->setChannelLayout(AudioChannelLayout::stereo());
		master->m_buffer->silenceAllChannels();

		auto source = AudioBuffer{frames, 6};
		for (ch_cnt_t channel = 0; channel < 6; ++channel)
		{
			for (f_cnt_t frame = 0; frame < frames; ++frame)
			{
				source.buffer(channel)[frame] = sampleValue(channel, frame);
			}
		}

		mixer->mixToChannel(source, AudioChannelLayout::surround51(), 0);

		const auto output = logicalBuffers(*master->m_buffer, master->channelLayout());
		QCOMPARE(output.channels(), ch_cnt_t{2});
		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			const auto* frameData = source.allBuffers().data();
			std::vector<float> values(6);
			for (ch_cnt_t channel = 0; channel < 6; ++channel)
			{
				values[channel] = frameData[channel][frame];
			}

			float expectedLeft = 0.f;
			float expectedRight = 0.f;
			downmixFrameToStereo(values.data(), AudioChannelLayout::surround51(),
				expectedLeft, expectedRight);

			QVERIFY(isClose(output.bufferPtr(0)[frame], expectedLeft));
			QVERIFY(isClose(output.bufferPtr(1)[frame], expectedRight));
		}
	}

	//! Mixer project data round-trips custom and preset layouts.
	void ProjectSettings_RoundTripLayouts()
	{
		const auto guard = Engine::audioEngine()->requestChangesGuard();
		auto* mixer = Engine::mixer();
		const auto index = mixer->createChannel();
		mixer->mixerChannel(index)->setChannelLayout(AudioChannelLayout::surround712());
		mixer->mixerChannel(0)->setChannelLayout(AudioChannelLayout::surround61());

		QDomDocument doc;
		auto root = doc.createElement(QStringLiteral("mixer"));
		doc.appendChild(root);
		mixer->saveSettings(doc, root);

		mixer->mixerChannel(index)->setChannelLayout(AudioChannelLayout::stereo());
		mixer->mixerChannel(0)->setChannelLayout(AudioChannelLayout::stereo());
		mixer->loadSettings(root);

		QCOMPARE(mixer->mixerChannel(index)->channelLayout().channels(), ch_cnt_t{10});
		QVERIFY(mixer->mixerChannel(index)->channelLayout() == AudioChannelLayout::surround712());
		QCOMPARE(mixer->mixerChannel(0)->channelLayout().channels(), ch_cnt_t{7});
		QVERIFY(mixer->mixerChannel(0)->channelLayout() == AudioChannelLayout::surround61());
		QCOMPARE(Engine::audioEngine()->masterLayout().channels(), ch_cnt_t{7});

		mixer->deleteChannel(index);
	}

	//! WAV export keeps all master channels and their order.
	void WaveExport_PreservesSurroundChannels()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const auto path = dir.filePath(QStringLiteral("surround.wav"));

		constexpr auto frames = f_cnt_t{16};
		auto source = AudioBuffer{frames, 6};
		for (ch_cnt_t channel = 0; channel < 6; ++channel)
		{
			for (f_cnt_t frame = 0; frame < frames; ++frame)
			{
				source.buffer(channel)[frame] = sampleValue(channel, frame);
			}
		}

		{
			auto settings = OutputSettings{48000, 160, OutputSettings::BitDepth::Depth32Bit};
			bool successful = false;
			auto device = AudioFileWave{settings, AudioChannelLayout::surround51(), successful, path,
				Engine::audioEngine()};
			QVERIFY(successful);
			device.writeBuffer(logicalBuffers(source, AudioChannelLayout::surround51()),
				AudioChannelLayout::surround51());
		}

		SF_INFO info{};
		auto* file = sf_open(path.toUtf8().constData(), SFM_READ, &info);
		QVERIFY(file != nullptr);
		QCOMPARE(info.channels, 6);
		QCOMPARE(info.frames, static_cast<sf_count_t>(frames));

		auto interleaved = std::vector<float>(frames * 6);
		QCOMPARE(sf_readf_float(file, interleaved.data(), frames), static_cast<sf_count_t>(frames));
		sf_close(file);

		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			for (ch_cnt_t channel = 0; channel < 6; ++channel)
			{
				QVERIFY2(isClose(interleaved[frame * 6 + channel], sampleValue(channel, frame)),
					qPrintable(QStringLiteral("channel %1 frame %2").arg(channel).arg(frame)));
			}
		}
	}

	//! A stereo file device receives an explicit surround-to-stereo fold.
	void WaveExport_DownmixesToStereoDeviceLayout()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const auto path = dir.filePath(QStringLiteral("stereo.wav"));

		constexpr auto frames = f_cnt_t{8};
		auto source = AudioBuffer{frames, 6};
		for (ch_cnt_t channel = 0; channel < 6; ++channel)
		{
			for (f_cnt_t frame = 0; frame < frames; ++frame)
			{
				source.buffer(channel)[frame] = sampleValue(channel, frame);
			}
		}

		{
			auto settings = OutputSettings{48000, 160, OutputSettings::BitDepth::Depth32Bit};
			bool successful = false;
			auto device = AudioFileWave{settings, AudioChannelLayout::stereo(), successful, path,
				Engine::audioEngine()};
			QVERIFY(successful);
			device.writeBuffer(logicalBuffers(source, AudioChannelLayout::surround51()),
				AudioChannelLayout::surround51());
		}

		SF_INFO info{};
		auto* file = sf_open(path.toUtf8().constData(), SFM_READ, &info);
		QVERIFY(file != nullptr);
		QCOMPARE(info.channels, 2);
		auto interleaved = std::vector<float>(frames * 2);
		QCOMPARE(sf_readf_float(file, interleaved.data(), frames), static_cast<sf_count_t>(frames));
		sf_close(file);

		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			std::vector<float> values(6);
			for (ch_cnt_t channel = 0; channel < 6; ++channel)
			{
				values[channel] = source.buffer(channel)[frame];
			}
			float expectedLeft = 0.f;
			float expectedRight = 0.f;
			downmixFrameToStereo(values.data(), AudioChannelLayout::surround51(),
				expectedLeft, expectedRight);
			QVERIFY(isClose(interleaved[frame * 2], expectedLeft));
			QVERIFY(isClose(interleaved[frame * 2 + 1], expectedRight));
		}
	}

	//! Sends from a surround channel to a stereo channel preserve stereo fold.
	void MixToChannel_StereoToSurroundUsesFrontPair()
	{
		const auto guard = Engine::audioEngine()->requestChangesGuard();
		constexpr auto frames = f_cnt_t{4};
		auto* mixer = Engine::mixer();
		auto* master = mixer->mixerChannel(0);
		master->setChannelLayout(AudioChannelLayout::surround51());
		master->m_buffer->silenceAllChannels();

		auto source = AudioBuffer{frames, 2};
		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			source.buffer(0)[frame] = 0.25f;
			source.buffer(1)[frame] = -0.25f;
		}

		mixer->mixToChannel(source, AudioChannelLayout::stereo(), 0);

		const auto output = logicalBuffers(*master->m_buffer, master->channelLayout());
		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			QVERIFY(isClose(output.bufferPtr(0)[frame], 0.25f));
			QVERIFY(isClose(output.bufferPtr(1)[frame], -0.25f));
			for (ch_cnt_t channel = 2; channel < 6; ++channel)
			{
				QVERIFY(isClose(output.bufferPtr(channel)[frame], 0.f));
			}
		}
	}
};

QTEST_GUILESS_MAIN(MixerLayoutTest)

#include "MixerLayoutTest.moc"
