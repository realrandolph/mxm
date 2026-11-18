/*
 * NoteTest.cpp
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
 */

#include <QtTest>
#include <QDomDocument>

#include <memory>

#include "Engine.h"
#include "InstrumentTrack.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "Song.h"

class NoteTest : public QObject
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

	void midiChannelRoundTrips()
	{
		using namespace mxm;
		QDomDocument doc;
		auto root = doc.createElement("root");
		doc.appendChild(root);

		Note source;
		source.setMidiChannel(14);
		source.saveState(doc, root);

		Note restored;
		restored.restoreState(root.firstChildElement("note"));
		QCOMPARE(restored.midiChannel(), 14);

		Note copied(source);
		QCOMPARE(copied.midiChannel(), 14);

		Note assigned;
		assigned = source;
		QCOMPARE(assigned.midiChannel(), 14);

		std::unique_ptr<Note> cloned(source.clone());
		QCOMPARE(cloned->midiChannel(), 14);
	}

	void midiChannelIsClampedToMidiRange()
	{
		using namespace mxm;
		Note note;
		note.setMidiChannel(MidiChannelCount);
		QCOMPARE(note.midiChannel(), MidiChannelCount - 1);
		note.setMidiChannel(-2);
		QCOMPARE(note.midiChannel(), -1);
	}

	void oldNotesDefaultToUnassignedChannel()
	{
		using namespace mxm;
		QDomDocument doc;
		auto noteElement = doc.createElement("note");
		noteElement.setAttribute("key", 60);
		noteElement.setAttribute("vol", 100);
		noteElement.setAttribute("pan", 0);
		noteElement.setAttribute("len", 96);
		noteElement.setAttribute("pos", 0);

		Note note;
		note.restoreState(noteElement);
		QCOMPARE(note.midiChannel(), -1);
	}

	void midiInputKeepsUnspecifiedSourceChannel()
	{
		using namespace mxm;
		InstrumentTrack track(Engine::getSong());
		NotePlayHandle noteHandle(&track, 0, 1, Note(), nullptr, -1,
			NotePlayHandle::Origin::MidiInput);

		QCOMPARE(static_cast<const Note&>(noteHandle).midiChannel(), -1);
		QCOMPARE(noteHandle.midiChannel(), track.midiPort()->realOutputChannel());
	}
};

QTEST_GUILESS_MAIN(NoteTest)
#include "NoteTest.moc"
