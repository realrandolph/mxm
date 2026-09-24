/*
 * TrackRenameLineEdit.h - class TrackRenameLineEdit
 *
 * Copyright (c) 2004-2008 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 * Copyright (c) 2017 Alexandre Almeida <http://m374lx.users.sourceforge.net/>
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

#ifndef MXM_GUI_TRACK_RENAME_LINE_EDIT_H
#define MXM_GUI_TRACK_RENAME_LINE_EDIT_H

#include <QLineEdit>

namespace mxm::gui
{

class TrackRenameLineEdit : public QLineEdit
{
	Q_OBJECT
public:
	TrackRenameLineEdit( QWidget * parent );
	void show();
	
protected:
	void keyPressEvent( QKeyEvent * ke ) override;
	
private:
	QString m_oldName;
} ;

} // namespace mxm::gui

#endif // MXM_GUI_TRACK_RENAME_LINE_EDIT_H
