/*
 * SideBar.h - side-bar in MXM's MainWindow
 *
 * Copyright (c) 2004-2009 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef MXM_GUI_SIDE_BAR_H
#define MXM_GUI_SIDE_BAR_H

#include <QMap>
#include <QButtonGroup>
#include <QToolBar>

class QToolButton;

namespace mxm::gui
{

class SideBarWidget;


class SideBar : public QToolBar
{
	Q_OBJECT
public:
	SideBar( Qt::Orientation _orientation, QWidget * _parent );
	~SideBar() override = default;

	void appendTab( SideBarWidget * _sbw );


private slots:
	void toggleButton( QAbstractButton * _btn );


private:
	QButtonGroup m_btnGroup;
	using ButtonMap = QMap<QToolButton*, QWidget*>;
	ButtonMap m_widgets;

} ;

} // namespace mxm::gui

#endif // MXM_GUI_SIDE_BAR_H
