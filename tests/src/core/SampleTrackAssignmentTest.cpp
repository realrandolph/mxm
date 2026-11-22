/*
 * SampleTrackAssignmentTest.cpp - sample track input assignment persistence
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

#include "SampleTrack.h"

#include <QtTest>

#include <QDomDocument>

#include "Engine.h"
#include "Song.h"

using namespace mxm;

namespace
{

auto makeTrackElement(QDomDocument& doc) -> QDomElement
{
	auto element = doc.createElement(QStringLiteral("track"));
	doc.appendChild(element);
	return element;
}

} // namespace

class SampleTrackAssignmentTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase() { Engine::init(true); }

	void cleanupTestCase() { Engine::destroy(); }

	//! The saved input endpoint survives a project round trip
	void Assignment_SaveLoadRoundTrip()
	{
		auto* song = Engine::getSong();
		auto track = SampleTrack{song};
		track.setInputAssignment(QStringLiteral("portaudio:USB Mic"));
		QCOMPARE(track.inputSourceId(), QStringLiteral("portaudio:USB Mic"));

		auto doc = QDomDocument{};
		auto element = makeTrackElement(doc);
		track.saveTrackSpecificSettings(doc, element, false);
		QCOMPARE(element.attribute(QStringLiteral("input_source")),
			QStringLiteral("portaudio:USB Mic"));

		auto loaded = SampleTrack{song};
		loaded.loadTrackSpecificSettings(element);
		QCOMPARE(loaded.inputSourceId(), QStringLiteral("portaudio:USB Mic"));

		track.setInputAssignment(QStringLiteral("auto"));
		track.saveTrackSpecificSettings(doc, element, false);
		loaded.loadTrackSpecificSettings(element);
		QCOMPARE(loaded.inputSourceId(), QStringLiteral("system-default"));
	}

	//! New and legacy projects without an explicit input default to the system input.
	void Assignment_MissingAttributesDefault()
	{
		auto* song = Engine::getSong();
		auto track = SampleTrack{song};
		track.setInputAssignment(QStringLiteral("portaudio:USB Mic"));

		auto doc = QDomDocument{};
		auto element = makeTrackElement(doc);
		track.loadTrackSpecificSettings(element);

		QCOMPARE(track.inputSourceId(), QStringLiteral("system-default"));
	}

	//! An unavailable saved id is reported and never resolved silently
	void Assignment_UnavailableSavedIdIsReported()
	{
		auto* song = Engine::getSong();
		auto track = SampleTrack{song};
		track.setInputAssignment(QStringLiteral("portaudio:Missing Mic"));

		QVERIFY(!track.inputAssignmentStatus().isEmpty());
		QVERIFY(track.resolvedInputSource() == std::nullopt);
	}
};

QTEST_GUILESS_MAIN(SampleTrackAssignmentTest)
#include "SampleTrackAssignmentTest.moc"
