/*
 * SampleClipChannelTest.cpp - multichannel sample clip serialization
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

#include "SampleClip.h"
#include "SampleTrack.h"

#include <QtTest>

#include <QDomDocument>

#include <array>
#include <memory>
#include <vector>

#include "Engine.h"
#include "Song.h"

using namespace mxm;

namespace
{

auto makeBuffer(ch_cnt_t channels, AudioChannelLayout layout, int sampleRate) -> std::shared_ptr<const SampleBuffer>
{
	constexpr auto frames = f_cnt_t{5};
	auto samples = std::vector<float>(frames * channels);
	for (f_cnt_t frame = 0; frame < frames; ++frame)
	{
		for (ch_cnt_t channel = 0; channel < channels; ++channel)
		{
			samples[frame * channels + channel]
				= static_cast<float>(frame) * 100.f + static_cast<float>(channel);
		}
	}
	return std::make_shared<const SampleBuffer>(std::move(samples), channels, std::move(layout), sampleRate);
}

auto saveClip(SampleClip& clip, QDomDocument& doc) -> QDomElement
{
	auto root = doc.createElement(QStringLiteral("clip"));
	doc.appendChild(root);
	clip.saveState(doc, root);
	return root.firstChildElement();
}

} // namespace

class SampleClipChannelTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase() { Engine::init(true); }
	void cleanupTestCase() { Engine::destroy(); }

	//! Embedded clip data preserves channel count, layout and every sample
	void EmbeddedRoundTrip_PreservesChannels()
	{
		const auto cases = std::array<std::pair<ch_cnt_t, AudioChannelLayout>, 4>{{
			{1, AudioChannelLayout::mono()},
			{2, AudioChannelLayout::stereo()},
			{6, AudioChannelLayout::surround51()},
			{8, AudioChannelLayout::discrete(8)},
		}};

		auto* song = Engine::getSong();
		auto* track = new SampleTrack(song);
		auto* clip = new SampleClip(track);

		for (const auto& [channels, layout] : cases)
		{
			clip->setSampleBuffer(makeBuffer(channels, layout, 48000));
			auto doc = QDomDocument{};
			const auto node = saveClip(*clip, doc);

			auto* loadedTrack = new SampleTrack(song);
			auto* loadedClip = new SampleClip(loadedTrack);
			loadedClip->restoreState(node);

			const auto& buffer = loadedClip->sample().buffer();
			QCOMPARE(buffer->channels(), channels);
			QCOMPARE(buffer->layout().channels(), channels);
			QVERIFY(buffer->layout() == layout);
			QCOMPARE(buffer->frames(), f_cnt_t{5});
			QCOMPARE(buffer->sampleRate(), sample_rate_t{48000});
			for (f_cnt_t frame = 0; frame < buffer->frames(); ++frame)
			{
				for (ch_cnt_t channel = 0; channel < channels; ++channel)
				{
					const auto expected = static_cast<float>(frame) * 100.f + static_cast<float>(channel);
					QCOMPARE(buffer->sample(frame, channel), expected);
				}
			}

			delete loadedClip;
			delete loadedTrack;
		}

		delete clip;
		delete track;
	}

	//! Older stereo clips without channel metadata still load as stereo
	void LegacyStereoProject_LoadsAsStereo()
	{
		const auto buffer = makeBuffer(2, AudioChannelLayout::stereo(), 44100);
		auto* song = Engine::getSong();
		auto* track = new SampleTrack(song);
		auto* clip = new SampleClip(track);
		clip->setSampleBuffer(buffer);

		auto doc = QDomDocument{};
		auto node = saveClip(*clip, doc);
		QVERIFY(node.hasAttribute("data"));
		node.removeAttribute("channels");
		node.removeAttribute("channel_layout");

		auto* loadedTrack = new SampleTrack(song);
		auto* loadedClip = new SampleClip(loadedTrack);
		loadedClip->restoreState(node);

		const auto& loaded = loadedClip->sample().buffer();
		QCOMPARE(loaded->channels(), DEFAULT_CHANNELS);
		QVERIFY(loaded->layout() == AudioChannelLayout::stereo());
		QCOMPARE(loaded->frames(), buffer->frames());
		for (f_cnt_t frame = 0; frame < buffer->frames(); ++frame)
		{
			QCOMPARE(loaded->sample(frame, 0), buffer->sample(frame, 0));
			QCOMPARE(loaded->sample(frame, 1), buffer->sample(frame, 1));
		}

		delete loadedClip;
		delete loadedTrack;
		delete clip;
		delete track;
	}
};

QTEST_GUILESS_MAIN(SampleClipChannelTest)
#include "SampleClipChannelTest.moc"
