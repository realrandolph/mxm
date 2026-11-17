/*
 * MidiClipTest.cpp
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
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#include <QtTest>
#include <QDomDocument>

#include <memory>

#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Song.h"

class MidiClipTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		mxm::Engine::init(true);
	}

	void cleanupTestCase()
	{
		mxm::Engine::destroy();
	}

	void noteChannelsSurviveCopyAndSerialization()
	{
		using namespace mxm;

		InstrumentTrack track(Engine::getSong());
		MidiClip clip(&track);
		Note note(TimePos(96), TimePos(0), 60);
		auto* channelNote = clip.addNote(note, false, 14);
		clip.addNote(Note(TimePos(96), TimePos(96), 64), false);

		QCOMPARE(clip.midiChannelForNote(channelNote), 14);
		QCOMPARE(clip.midiChannelForNote(clip.notes().back()), -1);

		std::unique_ptr<MidiClip> clone(clip.clone());
		QCOMPARE(clone->midiChannelForNote(clone->notes().front()), 14);
		QCOMPARE(clone->midiChannelForNote(clone->notes().back()), -1);

		QDomDocument doc;
		auto clipElement = doc.createElement("midiclip");
		doc.appendChild(clipElement);
		clip.exportToXML(doc, clipElement);

		MidiClip restored(&track);
		restored.loadSettings(clipElement);
		QCOMPARE(restored.notes().size(), std::size_t(2));
		QCOMPARE(restored.midiChannelForNote(restored.notes().front()), 14);
		QCOMPARE(restored.midiChannelForNote(restored.notes().back()), -1);
	}
};

QTEST_GUILESS_MAIN(MidiClipTest)
#include "MidiClipTest.moc"
