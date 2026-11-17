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

#include "Note.h"

class NoteTest : public QObject
{
	Q_OBJECT
private slots:
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
};

QTEST_GUILESS_MAIN(NoteTest)
#include "NoteTest.moc"
