/*
 * AudioChannelLayoutTest.cpp - channel layouts, speaker roles and downmixing
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

#include "AudioChannelLayout.h"

#include <QtTest>

#include <array>
#include <cmath>
#include <vector>

using namespace mxm;

namespace
{

//! Documented downmix gains: centre and surround at -3 dB, LFE at -6 dB.
constexpr auto CentreAndSurroundGain = 0.70710678f;
constexpr auto LfeGain = 0.5f;
constexpr auto GainTolerance = 1e-5f;

struct StereoLevels
{
	float left = 0.f;
	float right = 0.f;
};

auto downmix(const std::vector<float>& frame, const AudioChannelLayout& layout) -> StereoLevels
{
	auto levels = StereoLevels{};
	downmixFrameToStereo(frame.data(), layout, levels.left, levels.right);
	return levels;
}

auto isClose(float actual, float expected) -> bool
{
	return std::abs(actual - expected) <= GainTolerance;
}

} // namespace

class AudioChannelLayoutTest : public QObject
{
	Q_OBJECT

private slots:
	//! Round trips every named layout through `toString()` and `fromString()`
	void RoundTrip_StandardLayouts()
	{
		const auto cases = std::array<std::pair<AudioChannelLayout, const char*>, 8>{{
			{AudioChannelLayout::mono(), "mono"},
			{AudioChannelLayout::stereo(), "stereo"},
			{AudioChannelLayout::quadraphonic(), "4.0"},
			{AudioChannelLayout::surround50(), "5.0"},
			{AudioChannelLayout::surround51(), "5.1"},
			{AudioChannelLayout::surround61(), "6.1"},
			{AudioChannelLayout::surround71(), "7.1"},
			{AudioChannelLayout::surround712(), "7.1.2"},
		}};

		for (const auto& [layout, text] : cases)
		{
			QCOMPARE(layout.toString(), QString::fromLatin1(text));
			const auto parsed = AudioChannelLayout::fromString(QString::fromLatin1(text));
			QVERIFY(parsed == layout);
			QCOMPARE(parsed.channels(), layout.channels());
			QVERIFY(parsed.hasKnownRoles());
		}
	}

	//! Discrete layouts keep their channel count and have no speaker roles
	void RoundTrip_DiscreteLayouts()
	{
		for (const auto channels : {ch_cnt_t{0}, ch_cnt_t{1}, ch_cnt_t{2}, ch_cnt_t{6}, ch_cnt_t{8}})
		{
			const auto layout = AudioChannelLayout::discrete(channels);
			QCOMPARE(layout.channels(), channels);
			QVERIFY(!layout.hasKnownRoles());
			QVERIFY(layout.isDiscrete());
			for (ch_cnt_t channel = 0; channel < channels; ++channel)
			{
				QCOMPARE(static_cast<int>(layout.role(channel)), static_cast<int>(SpeakerRole::Unknown));
			}

			const auto text = QStringLiteral("discrete:%1").arg(channels);
			QCOMPARE(layout.toString(), text);
			const auto parsed = AudioChannelLayout::fromString(text);
			QVERIFY(parsed == layout);
			QCOMPARE(parsed.channels(), channels);
		}

		// A discrete six channel layout is not the same thing as 5.1
		QVERIFY(AudioChannelLayout::discrete(6) != AudioChannelLayout::surround51());
		QVERIFY(AudioChannelLayout::discrete(8) != AudioChannelLayout::surround71());
	}

	//! Standard role assignments for known channel counts
	void RolesFor_StandardLayouts()
	{
		const auto expect = [](ch_cnt_t channels, const std::vector<SpeakerRole>& expected) {
			const auto roles = AudioChannelLayout::rolesFor(channels);
			for (size_t index = 0; index < expected.size(); ++index)
			{
				QCOMPARE(static_cast<int>(roles[index]), static_cast<int>(expected[index]));
			}
			QCOMPARE(static_cast<int>(roles[expected.size()]), static_cast<int>(SpeakerRole::Unknown));
		};

		expect(1, {SpeakerRole::FrontCenter});
		expect(2, {SpeakerRole::FrontLeft, SpeakerRole::FrontRight});
		expect(3, {SpeakerRole::FrontLeft, SpeakerRole::FrontRight, SpeakerRole::FrontCenter});
		expect(4, {SpeakerRole::FrontLeft, SpeakerRole::FrontRight, SpeakerRole::BackLeft, SpeakerRole::BackRight});
		expect(5,
			{SpeakerRole::FrontLeft, SpeakerRole::FrontRight, SpeakerRole::FrontCenter, SpeakerRole::BackLeft,
				SpeakerRole::BackRight});
		expect(6,
			{SpeakerRole::FrontLeft, SpeakerRole::FrontRight, SpeakerRole::FrontCenter, SpeakerRole::Lfe,
				SpeakerRole::BackLeft, SpeakerRole::BackRight});
		expect(7,
			{SpeakerRole::FrontLeft, SpeakerRole::FrontRight, SpeakerRole::FrontCenter, SpeakerRole::Lfe,
				SpeakerRole::BackLeft, SpeakerRole::BackRight, SpeakerRole::BackCenter});
		expect(8,
			{SpeakerRole::FrontLeft, SpeakerRole::FrontRight, SpeakerRole::FrontCenter, SpeakerRole::Lfe,
				SpeakerRole::BackLeft, SpeakerRole::BackRight, SpeakerRole::SideLeft, SpeakerRole::SideRight});
		expect(10,
			{SpeakerRole::FrontLeft, SpeakerRole::FrontRight, SpeakerRole::FrontCenter, SpeakerRole::Lfe,
				SpeakerRole::BackLeft, SpeakerRole::BackRight, SpeakerRole::SideLeft, SpeakerRole::SideRight,
				SpeakerRole::TopFrontLeft, SpeakerRole::TopFrontRight});

		// Channel counts without a standard layout have no roles at all
		for (const auto channels : {ch_cnt_t{9}, ch_cnt_t{11}, ch_cnt_t{12}})
		{
			const auto roles = AudioChannelLayout::rolesFor(channels);
			QCOMPARE(static_cast<int>(roles[0]), static_cast<int>(SpeakerRole::Unknown));
			QCOMPARE(static_cast<int>(roles[channels - 1]), static_cast<int>(SpeakerRole::Unknown));
			QVERIFY(!AudioChannelLayout::standard(channels).hasKnownRoles());
		}

		QVERIFY(AudioChannelLayout::mono().hasKnownRoles());
		QVERIFY(AudioChannelLayout::stereo().hasKnownRoles());
		QVERIFY(AudioChannelLayout::standard(3).hasKnownRoles());
		QVERIFY(AudioChannelLayout::quadraphonic().hasKnownRoles());
		QVERIFY(AudioChannelLayout::surround50().hasKnownRoles());
		QVERIFY(AudioChannelLayout::surround51().hasKnownRoles());
		QVERIFY(AudioChannelLayout::surround61().hasKnownRoles());
		QVERIFY(AudioChannelLayout::surround71().hasKnownRoles());
		QVERIFY(AudioChannelLayout::surround712().hasKnownRoles());
		QVERIFY(!AudioChannelLayout{}.hasKnownRoles());
		QVERIFY(!AudioChannelLayout::discrete(6).hasKnownRoles());
	}

	//! `fromString()` handles whitespace, case, missing layouts and unknown text
	void FromString_Fallbacks()
	{
		QVERIFY(AudioChannelLayout::fromString(QString{}) == AudioChannelLayout::stereo());
		QCOMPARE(AudioChannelLayout::fromString(QString{}).channels(), ch_cnt_t{2});
		QVERIFY(AudioChannelLayout::fromString(QStringLiteral("stereo"), 8) == AudioChannelLayout::stereo());
		QVERIFY(AudioChannelLayout::fromString(QStringLiteral(" 4.0 ")) == AudioChannelLayout::quadraphonic());
		QVERIFY(AudioChannelLayout::fromString(QStringLiteral("quad")) == AudioChannelLayout::quadraphonic());
		QVERIFY(AudioChannelLayout::fromString(QStringLiteral(" 5.0 ")) == AudioChannelLayout::surround50());
		QVERIFY(AudioChannelLayout::fromString(QStringLiteral(" 5.1 ")) == AudioChannelLayout::surround51());
		QVERIFY(AudioChannelLayout::fromString(QStringLiteral("6.1")) == AudioChannelLayout::surround61());
		QVERIFY(AudioChannelLayout::fromString(QStringLiteral("7.1")) == AudioChannelLayout::surround71());
		QVERIFY(AudioChannelLayout::fromString(QStringLiteral("7.1.2")) == AudioChannelLayout::surround712());

		const auto unknown = AudioChannelLayout::fromString(QStringLiteral("nonsense"), 6);
		QCOMPARE(unknown.channels(), ch_cnt_t{6});
		QVERIFY(unknown.isDiscrete());
		QVERIFY(unknown == AudioChannelLayout::discrete(6));
	}

	//! Layouts without a named form round trip through their role list
	void RoundTrip_RoleList()
	{
		const auto layout = AudioChannelLayout::standard(3);
		QVERIFY(layout.hasKnownRoles());
		QCOMPARE(layout.toString(), QStringLiteral("roles:FL,FR,FC"));

		const auto parsed = AudioChannelLayout::fromString(layout.toString());
		QVERIFY(parsed == layout);
		QCOMPARE(parsed.channels(), ch_cnt_t{3});
	}

	//! Channel counts above the maximum are clamped
	void Channels_ClampedToMaximum()
	{
		const auto layout = AudioChannelLayout{static_cast<ch_cnt_t>(MaxChannelsPerLayout + 32)};
		QCOMPARE(layout.channels(), MaxChannelsPerLayout);
		QVERIFY(!layout.hasKnownRoles());
	}

	//! Stereo downmixing is an exact passthrough
	void Downmix_StereoPassthrough()
	{
		const auto layout = AudioChannelLayout::stereo();
		const auto levels = downmix({0.25f, -0.5f}, layout);
		QVERIFY(levels.left == 0.25f);
		QVERIFY(levels.right == -0.5f);
	}

	//! A single channel is a mono signal and is duplicated to both outputs
	void Downmix_MonoDuplicated()
	{
		const auto layout = AudioChannelLayout::mono();
		const auto levels = downmix({0.3f}, layout);
		QVERIFY(levels.left == 0.3f);
		QVERIFY(levels.right == 0.3f);
	}

	//! 5.1 folds every channel into stereo with the documented gains
	void Downmix_Surround51()
	{
		const auto layout = AudioChannelLayout::surround51();
		const auto frame = std::vector<float>{1.f, 2.f, 3.f, 4.f, 5.f, 6.f};
		const auto levels = downmix(frame, layout);

		// Back channels are folded into their side of the stereo image at -3 dB;
		// the centre is also -3 dB and the LFE is -6 dB.
		const auto expectedLeft = 1.f + 3.f * CentreAndSurroundGain + 4.f * LfeGain
			+ 5.f * CentreAndSurroundGain;
		const auto expectedRight = 2.f + 3.f * CentreAndSurroundGain + 4.f * LfeGain
			+ 6.f * CentreAndSurroundGain;

		QVERIFY2(isClose(levels.left, expectedLeft),
			qPrintable(QStringLiteral("left %1 expected %2").arg(levels.left).arg(expectedLeft)));
		QVERIFY2(isClose(levels.right, expectedRight),
			qPrintable(QStringLiteral("right %1 expected %2").arg(levels.right).arg(expectedRight)));
		QVERIFY(levels.left > 0.f);
		QVERIFY(levels.right > 0.f);
		QVERIFY(std::abs(levels.left - levels.right) > 1.f);

		// Every source channel must contribute exactly its documented gain and
		// must not disappear from the stereo mix.
		const auto contributions = std::array<StereoLevels, 6>{{
			{1.f, 0.f},
			{0.f, 2.f},
			{3.f * CentreAndSurroundGain, 3.f * CentreAndSurroundGain},
			{4.f * LfeGain, 4.f * LfeGain},
			{5.f * CentreAndSurroundGain, 0.f},
			{0.f, 6.f * CentreAndSurroundGain},
		}};

		for (ch_cnt_t channel = 0; channel < layout.channels(); ++channel)
		{
			auto reduced = frame;
			reduced[channel] = 0.f;
			const auto withoutChannel = downmix(reduced, layout);

			QVERIFY(isClose(levels.left - withoutChannel.left, contributions[channel].left));
			QVERIFY(isClose(levels.right - withoutChannel.right, contributions[channel].right));
		}
	}

	//! Discrete layouts keep the channel order and never drop a channel
	void Downmix_DiscreteLayouts()
	{
		for (const auto channels : {ch_cnt_t{6}, ch_cnt_t{8}})
		{
			const auto layout = AudioChannelLayout::discrete(channels);
			auto frame = std::vector<float>(channels);
			for (ch_cnt_t channel = 0; channel < channels; ++channel)
			{
				frame[channel] = static_cast<float>(channel + 1);
			}

			const auto levels = downmix(frame, layout);
			QVERIFY(levels.left > 0.f);
			QVERIFY(levels.right > 0.f);
			QVERIFY(levels.left != levels.right);

			for (ch_cnt_t channel = 0; channel < channels; ++channel)
			{
				auto reduced = frame;
				reduced[channel] = 0.f;
				const auto withoutChannel = downmix(reduced, layout);
				const auto leftContribution = levels.left - withoutChannel.left;
				const auto rightContribution = levels.right - withoutChannel.right;

				// No channel is silently ignored
				QVERIFY(std::abs(leftContribution) > 0.f || std::abs(rightContribution) > 0.f);
				QVERIFY(leftContribution * rightContribution <= 0.f);

				const auto expectedMagnitude = channel < 2 ? frame[channel]
														   : frame[channel] * CentreAndSurroundGain;

				// Channel 0 feeds the left output, channel 1 the right output,
				// and the remaining channels alternate without changing order.
				if (channel % 2 == 0)
				{
					QVERIFY(isClose(leftContribution, expectedMagnitude));
					QVERIFY(isClose(rightContribution, 0.f));
				}
				else
				{
					QVERIFY(isClose(leftContribution, 0.f));
					QVERIFY(isClose(rightContribution, expectedMagnitude));
				}
			}
		}
	}

	//! Block downmixers duplicate a mono block exactly like the frame mapping
	void DownmixBlocks_MonoDuplicated()
	{
		constexpr auto frames = f_cnt_t{3};
		const auto layout = AudioChannelLayout::mono();
		auto mono = std::vector<float>{0.25f, -0.5f, 1.f};
		const auto* monoPointers = mono.data();

		auto interleavedDestination = std::vector<float>(frames * 2, 0.f);
		downmixToStereo(PlanarBufferView<const float>{&monoPointers, 1, frames}, layout,
			InterleavedBufferView<float, 2>{interleavedDestination.data(), frames});

		auto planarLeft = std::vector<float>(frames, 0.f);
		auto planarRight = std::vector<float>(frames, 0.f);
		const auto planarDestination = std::array<float*, 2>{planarLeft.data(), planarRight.data()};
		downmixToStereo(InterleavedBufferView<const float>{mono.data(), 1, frames}, layout,
			PlanarBufferView<float, 2>{planarDestination.data(), frames});

		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			QCOMPARE(interleavedDestination[frame * 2], mono[frame]);
			QCOMPARE(interleavedDestination[frame * 2 + 1], mono[frame]);
			QCOMPARE(planarLeft[frame], mono[frame]);
			QCOMPARE(planarRight[frame], mono[frame]);
		}
	}

	//! The planar and interleaved block downmixers agree with the frame mapping
	void DownmixBlocks_MatchFrameMapping()
	{
		constexpr auto frames = f_cnt_t{4};
		const auto layout = AudioChannelLayout::surround51();
		const auto channels = layout.channels();

		auto interleaved = std::vector<float>(frames * channels);
		auto planar = std::vector<std::vector<float>>(channels, std::vector<float>(frames));
		auto planarPointers = std::vector<const float*>(channels);

		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			for (ch_cnt_t channel = 0; channel < channels; ++channel)
			{
				const auto value = static_cast<float>(frame + 1) + static_cast<float>(channel) * 10.f;
				interleaved[frame * channels + channel] = value;
				planar[channel][frame] = value;
			}
		}
		for (ch_cnt_t channel = 0; channel < channels; ++channel)
		{
			planarPointers[channel] = planar[channel].data();
		}

		auto interleavedFromPlanar = std::vector<float>(frames * 2);
		downmixToStereo(PlanarBufferView<const float>{planarPointers.data(), channels, frames}, layout,
			InterleavedBufferView<float, 2>{interleavedFromPlanar.data(), frames});

		auto planarDestinationLeft = std::vector<float>(frames);
		auto planarDestinationRight = std::vector<float>(frames);
		const auto planarDestination
			= std::array<float*, 2>{planarDestinationLeft.data(), planarDestinationRight.data()};
		downmixToStereo(InterleavedBufferView<const float>{interleaved.data(), channels, frames}, layout,
			PlanarBufferView<float, 2>{planarDestination.data(), frames});

		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			float left = 0.f;
			float right = 0.f;
			downmixFrameToStereo(interleaved.data() + frame * channels, layout, left, right);

			QVERIFY(interleavedFromPlanar[frame * 2] == left);
			QVERIFY(interleavedFromPlanar[frame * 2 + 1] == right);
			QVERIFY(planarDestinationLeft[frame] == left);
			QVERIFY(planarDestinationRight[frame] == right);
		}
	}

	//! Preset registry entries, order, names, and lookup
	void PresetRegistry()
	{
		const auto& presets = AudioChannelLayout::presets();
		QCOMPARE(presets.size(), size_t{9});

		const auto expectedNames = std::array<const char*, 9>{
			"Mono", "Stereo", "3.0", "4.0 (Quad)", "5.0", "5.1", "6.1", "7.1", "7.1.2"
		};
		const auto expectedLayouts = std::array<AudioChannelLayout, 9>{
			AudioChannelLayout::mono(),
			AudioChannelLayout::stereo(),
			AudioChannelLayout::standard(3),
			AudioChannelLayout::quadraphonic(),
			AudioChannelLayout::surround50(),
			AudioChannelLayout::surround51(),
			AudioChannelLayout::surround61(),
			AudioChannelLayout::surround71(),
			AudioChannelLayout::surround712(),
		};

		for (size_t i = 0; i < 9; ++i)
		{
			QCOMPARE(presets[i].name, QString::fromLatin1(expectedNames[i]));
			QVERIFY(presets[i].layout == expectedLayouts[i]);
			QCOMPARE(AudioChannelLayout::presetIndex(expectedLayouts[i]), static_cast<int>(i));
			QVERIFY(AudioChannelLayout::fromPresetIndex(static_cast<int>(i)) == expectedLayouts[i]);
			QCOMPARE(expectedLayouts[i].displayName(), QString::fromLatin1(expectedNames[i]));
		}

		QCOMPARE(AudioChannelLayout::presetIndex(AudioChannelLayout::discrete(6)), -1);
		QCOMPARE(AudioChannelLayout::presetIndex(AudioChannelLayout{}), -1);
		QCOMPARE(AudioChannelLayout::fromPresetIndex(-1), AudioChannelLayout{});
		QCOMPARE(AudioChannelLayout::fromPresetIndex(9), AudioChannelLayout{});

		QCOMPARE(AudioChannelLayout{}.displayName(), QStringLiteral("Empty"));
		QCOMPARE(AudioChannelLayout::discrete(4).displayName(), QStringLiteral("4-channel discrete"));
	}

	//! Stereo to 5.1 preserves FL/FR and leaves all other channels zero
	void Convert_StereoTo51()
	{
		const auto srcLayout = AudioChannelLayout::stereo();
		const auto dstLayout = AudioChannelLayout::surround51();
		const auto src = std::vector<float>{1.5f, -2.5f};
		auto dst = std::vector<float>(6, 0.f);

		addFrameConverted(src.data(), srcLayout, dst.data(), dstLayout, 1.0f);

		QVERIFY(isClose(dst[0], 1.5f));  // FL
		QVERIFY(isClose(dst[1], -2.5f)); // FR
		QVERIFY(isClose(dst[2], 0.f));   // FC
		QVERIFY(isClose(dst[3], 0.f));   // LFE
		QVERIFY(isClose(dst[4], 0.f));   // BL
		QVERIFY(isClose(dst[5], 0.f));   // BR
	}

	//! 5.1 to stereo matches downmixFrameToStereo exactly
	void Convert_51ToStereo()
	{
		const auto srcLayout = AudioChannelLayout::surround51();
		const auto dstLayout = AudioChannelLayout::stereo();
		const auto src = std::vector<float>{1.f, 2.f, 3.f, 4.f, 5.f, 6.f};
		auto dst = std::vector<float>(2, 0.f);

		float expectedL = 0.f;
		float expectedR = 0.f;
		downmixFrameToStereo(src.data(), srcLayout, expectedL, expectedR);

		addFrameConverted(src.data(), srcLayout, dst.data(), dstLayout, 1.0f);

		QVERIFY(isClose(dst[0], expectedL));
		QVERIFY(isClose(dst[1], expectedR));
	}

	//! LFE is routed exclusively to dst LFE when present
	void Convert_LfeToLfe()
	{
		const auto srcLayout = AudioChannelLayout::surround51();
		const auto dstLayout = AudioChannelLayout::surround71();
		const auto src = std::vector<float>{0.f, 0.f, 0.f, 0.8f, 0.f, 0.f};
		auto dst = std::vector<float>(8, 0.f);

		addFrameConverted(src.data(), srcLayout, dst.data(), dstLayout, 1.0f);

		for (ch_cnt_t c = 0; c < 8; ++c)
		{
			if (c == 3) // LFE channel in 7.1
			{
				QVERIFY(isClose(dst[c], 0.8f));
			}
			else
			{
				QVERIFY(isClose(dst[c], 0.f));
			}
		}
	}

	//! Mono to 5.1 routes into FrontCenter
	void Convert_MonoTo51()
	{
		const auto srcLayout = AudioChannelLayout::mono();
		const auto dstLayout = AudioChannelLayout::surround51();
		const auto src = std::vector<float>{0.75f};
		auto dst = std::vector<float>(6, 0.f);

		addFrameConverted(src.data(), srcLayout, dst.data(), dstLayout, 1.0f);

		QVERIFY(isClose(dst[0], 0.f));   // FL
		QVERIFY(isClose(dst[1], 0.f));   // FR
		QVERIFY(isClose(dst[2], 0.75f)); // FC
		QVERIFY(isClose(dst[3], 0.f));   // LFE
		QVERIFY(isClose(dst[4], 0.f));   // BL
		QVERIFY(isClose(dst[5], 0.f));   // BR
	}

	//! Same-layout conversion adds into dst with gain without clearing
	void Convert_SameLayoutAdditive()
	{
		const auto layout = AudioChannelLayout::surround51();
		const auto src = std::vector<float>{1.f, 2.f, 3.f, 4.f, 5.f, 6.f};
		auto dst = std::vector<float>{10.f, 20.f, 30.f, 40.f, 50.f, 60.f};

		addFrameConverted(src.data(), layout, dst.data(), layout, 0.5f);

		QVERIFY(isClose(dst[0], 10.5f));
		QVERIFY(isClose(dst[1], 21.0f));
		QVERIFY(isClose(dst[2], 31.5f));
		QVERIFY(isClose(dst[3], 42.0f));
		QVERIFY(isClose(dst[4], 52.5f));
		QVERIFY(isClose(dst[5], 63.0f));
	}

	//! 7.1.2 to 5.1 maps known channels and folds top channels into FL/FR
	void Convert_712To51()
	{
		const auto srcLayout = AudioChannelLayout::surround712();
		const auto dstLayout = AudioChannelLayout::surround51();

		auto src = std::vector<float>(10, 0.f);
		src[0] = 1.f; // FL
		src[1] = 2.f; // FR
		src[2] = 3.f; // FC
		src[3] = 4.f; // LFE
		src[4] = 5.f; // BL
		src[5] = 6.f; // BR
		src[8] = 7.f; // TFL
		src[9] = 8.f; // TFR

		auto dst = std::vector<float>(6, 0.f);
		addFrameConverted(src.data(), srcLayout, dst.data(), dstLayout, 1.0f);

		const auto expectedFL = 1.f + (7.f + 8.f) * CentreAndSurroundGain;
		const auto expectedFR = 2.f + (7.f + 8.f) * CentreAndSurroundGain;

		QVERIFY2(isClose(dst[0], expectedFL),
			qPrintable(QStringLiteral("FL %1 expected %2").arg(dst[0]).arg(expectedFL)));
		QVERIFY2(isClose(dst[1], expectedFR),
			qPrintable(QStringLiteral("FR %1 expected %2").arg(dst[1]).arg(expectedFR)));
		QVERIFY(isClose(dst[2], 3.f)); // FC mapped
		QVERIFY(isClose(dst[3], 4.f)); // LFE mapped
		QVERIFY(isClose(dst[4], 5.f)); // BL mapped
		QVERIFY(isClose(dst[5], 6.f)); // BR mapped
	}

	//! Discrete 6 to discrete 8 maps by index and does not lose extra signal
	void Convert_Discrete6To8()
	{
		const auto srcLayout = AudioChannelLayout::discrete(6);
		const auto dstLayout = AudioChannelLayout::discrete(8);
		const auto src = std::vector<float>{1.f, 2.f, 3.f, 4.f, 5.f, 6.f};
		auto dst = std::vector<float>(8, 0.f);

		addFrameConverted(src.data(), srcLayout, dst.data(), dstLayout, 1.0f);

		for (ch_cnt_t c = 0; c < 6; ++c)
		{
			QVERIFY(isClose(dst[c], src[c]));
		}
		QVERIFY(isClose(dst[6], 0.f));
		QVERIFY(isClose(dst[7], 0.f));
	}

	//! Planar block conversion matches interleaved frame conversion
	void Convert_PlanarMatchesFrame()
	{
		constexpr auto frames = f_cnt_t{5};
		const auto srcLayout = AudioChannelLayout::surround712();
		const auto dstLayout = AudioChannelLayout::surround51();
		const auto srcChannels = srcLayout.channels();
		const auto dstChannels = dstLayout.channels();

		auto srcPlanar = std::vector<std::vector<float>>(srcChannels, std::vector<float>(frames));
		auto srcPointers = std::vector<const float*>(srcChannels);
		for (ch_cnt_t c = 0; c < srcChannels; ++c)
		{
			for (f_cnt_t f = 0; f < frames; ++f)
			{
				srcPlanar[c][f] = static_cast<float>(c * 10 + f + 1);
			}
			srcPointers[c] = srcPlanar[c].data();
		}

		auto dstPlanar = std::vector<std::vector<float>>(dstChannels, std::vector<float>(frames, 100.f));
		auto dstPointers = std::vector<float*>(dstChannels);
		for (ch_cnt_t c = 0; c < dstChannels; ++c)
		{
			dstPointers[c] = dstPlanar[c].data();
		}

		addPlanarConverted(
			PlanarBufferView<const float>{srcPointers.data(), srcChannels, frames},
			srcLayout,
			PlanarBufferView<float>{dstPointers.data(), dstChannels, frames},
			dstLayout,
			0.75f);

		for (f_cnt_t f = 0; f < frames; ++f)
		{
			auto frameSrc = std::vector<float>(srcChannels);
			for (ch_cnt_t c = 0; c < srcChannels; ++c)
			{
				frameSrc[c] = srcPlanar[c][f];
			}
			auto frameDst = std::vector<float>(dstChannels, 100.f);
			addFrameConverted(frameSrc.data(), srcLayout, frameDst.data(), dstLayout, 0.75f);

			for (ch_cnt_t c = 0; c < dstChannels; ++c)
			{
				QVERIFY(isClose(dstPlanar[c][f], frameDst[c]));
			}
		}
	}

	//! Planar fast path for same layout adds with gain
	void Convert_PlanarSameLayout()
	{
		constexpr auto frames = f_cnt_t{4};
		const auto layout = AudioChannelLayout::surround51();
		const auto channels = layout.channels();

		auto srcPlanar = std::vector<std::vector<float>>(channels, std::vector<float>(frames, 2.0f));
		auto srcPointers = std::vector<const float*>(channels);
		for (ch_cnt_t c = 0; c < channels; ++c)
		{
			srcPointers[c] = srcPlanar[c].data();
		}

		auto dstPlanar = std::vector<std::vector<float>>(channels, std::vector<float>(frames, 10.0f));
		auto dstPointers = std::vector<float*>(channels);
		for (ch_cnt_t c = 0; c < channels; ++c)
		{
			dstPointers[c] = dstPlanar[c].data();
		}

		addPlanarConverted(
			PlanarBufferView<const float>{srcPointers.data(), channels, frames},
			layout,
			PlanarBufferView<float>{dstPointers.data(), channels, frames},
			layout,
			0.5f);

		for (ch_cnt_t c = 0; c < channels; ++c)
		{
			for (f_cnt_t f = 0; f < frames; ++f)
			{
				QVERIFY(isClose(dstPlanar[c][f], 11.0f));
			}
		}
	}
};

QTEST_GUILESS_MAIN(AudioChannelLayoutTest)
#include "AudioChannelLayoutTest.moc"
