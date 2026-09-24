/*
 * SendButtonIndicator.h
 *
 * Copyright (c) 2014-2022 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef MXM_GUI_SEND_BUTTON_INDICATOR_H
#define MXM_GUI_SEND_BUTTON_INDICATOR_H

#include <QLabel>
#include "embed.h"


namespace mxm
{

class FloatModel;

namespace gui
{

class MixerChannelView;
class MixerView;

class SendButtonIndicator : public QLabel
{
public:
	SendButtonIndicator(QWidget* parent, MixerChannelView* owner, MixerView* mv);

	void mousePressEvent(QMouseEvent* e) override;
	void updateLightStatus();

private:

	MixerChannelView* m_parent;
	MixerView* m_mv;
	QPixmap m_qpmOff = embed::getIconPixmap("mixer_send_off", 29, 20);
	QPixmap m_qpmOn = embed::getIconPixmap("mixer_send_on", 29, 20);

	FloatModel* getSendModel();
};


} // namespace gui

} // namespace mxm

#endif // MXM_GUI_SEND_BUTTON_INDICATOR_H
