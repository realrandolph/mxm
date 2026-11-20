/*
 * SampleDecoderChannelTest.cpp - Vorbis channel order remapping
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

#include "SampleDecoder.h"

#include <QtTest>

#include <array>

using namespace mxm;

class SampleDecoderChannelTest : public QObject
{
	Q_OBJECT

private slots:
	//! Vorbis I standard channel order maps onto the canonical MXM layouts
	void VorbisChannelMap_StandardCounts_data()
	{
		QTest::addColumn<int>("channels");
		QTest::newRow("mono") << 1;
		QTest::newRow("stereo") << 2;
		QTest::newRow("3.0") << 3;
		QTest::newRow("quad") << 4;
		QTest::newRow("5.0") << 5;
		QTest::newRow("5.1") << 6;
		QTest::newRow("6.1") << 7;
		QTest::newRow("7.1") << 8;
	}

	void VorbisChannelMap_StandardCounts()
	{
		QFETCH(int, channels);
		const auto count = static_cast<ch_cnt_t>(channels);
		const auto map = SampleDecoder::vorbisChannelMap(count);

		QCOMPARE(map.layout.channels(), count);

		const auto expectedOrder = [&] {
			switch (count)
			{
			case 1: return std::array<ch_cnt_t, 8>{0};
			case 2: return std::array<ch_cnt_t, 8>{0, 1};
			case 3: return std::array<ch_cnt_t, 8>{0, 2, 1};
			case 4: return std::array<ch_cnt_t, 8>{0, 1, 2, 3};
			case 5: return std::array<ch_cnt_t, 8>{0, 2, 1, 3, 4};
			case 6: return std::array<ch_cnt_t, 8>{0, 2, 1, 5, 3, 4};
			case 7: return std::array<ch_cnt_t, 8>{0, 2, 1, 6, 3, 4, 5};
			case 8: return std::array<ch_cnt_t, 8>{0, 2, 1, 7, 5, 6, 3, 4};
			default: return std::array<ch_cnt_t, 8>{};
			}
		}();

		for (ch_cnt_t channel = 0; channel < count; ++channel)
		{
			QCOMPARE(map.order[channel], expectedOrder[channel]);
		}

		if (count == 6)
		{
			QVERIFY(map.layout == AudioChannelLayout::surround51());
		}
		else if (count == 8)
		{
			QVERIFY(map.layout == AudioChannelLayout::surround71());
		}
		else if (count == 1)
		{
			QVERIFY(map.layout == AudioChannelLayout::mono());
		}
		else if (count == 2)
		{
			QVERIFY(map.layout == AudioChannelLayout::stereo());
		}
	}

	//! 5.1 Vorbis order is FL, FC, FR, BL, BR, LFE and must become WAV order
	void VorbisChannelMap_51Roles()
	{
		const auto map = SampleDecoder::vorbisChannelMap(6);
		const auto expectedRoles = std::array{
			SpeakerRole::FrontLeft,
			SpeakerRole::FrontRight,
			SpeakerRole::FrontCenter,
			SpeakerRole::Lfe,
			SpeakerRole::BackLeft,
			SpeakerRole::BackRight,
		};
		for (ch_cnt_t channel = 0; channel < 6; ++channel)
		{
			QCOMPARE(static_cast<int>(map.layout.role(channel)),
				static_cast<int>(expectedRoles[channel]));
		}
	}

	//! Undefined Vorbis channel counts stay discrete and in stream order
	void VorbisChannelMap_UndefinedCounts()
	{
		for (const auto channels : {ch_cnt_t{9}, ch_cnt_t{16}})
		{
			const auto map = SampleDecoder::vorbisChannelMap(channels);
			QCOMPARE(map.layout.channels(), channels);
			QVERIFY(map.layout.isDiscrete());
			for (ch_cnt_t channel = 0; channel < 8; ++channel)
			{
				QCOMPARE(map.order[channel], channel);
			}
		}
	}

	//! Ogg/Vorbis files accepted by libsndfile still take the Vorbis remap path
	void DecodeMultichannelOgg_UsesCanonicalLayout()
	{
#ifdef MXM_TEST_HAVE_OGGVORBIS
		const auto path = QStringLiteral(MXM_TEST_DATA_DIR "/vorbis-6ch.ogg");
		const auto decoded = SampleDecoder::decode(path);
		QVERIFY(decoded.has_value());
		QCOMPARE(decoded->channels, ch_cnt_t{6});
		QVERIFY(decoded->layout == AudioChannelLayout::surround51());
		QVERIFY(!decoded->samples.empty());
#else
		QSKIP("Ogg/Vorbis decoder is not enabled in this build");
#endif
	}
};

QTEST_GUILESS_MAIN(SampleDecoderChannelTest)
#include "SampleDecoderChannelTest.moc"
