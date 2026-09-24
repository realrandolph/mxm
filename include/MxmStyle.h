/*
 * MxmStyle.h - the graphical style used by MXM to create a consistent
 *                interface
 *
 * Copyright (c) 2007-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef MXM_GUI_MXM_STYLE_H
#define MXM_GUI_MXM_STYLE_H

#include <QFileSystemWatcher>
#include <QProxyStyle>


namespace mxm::gui
{


class MxmStyle : public QProxyStyle
{
public:
	MxmStyle();
	~MxmStyle() override = default;

	QPalette standardPalette() const override;

	void drawComplexControl(
				ComplexControl control,
				const QStyleOptionComplex * option,
					QPainter *painter,
						const QWidget *widget ) const override;
	void drawPrimitive( PrimitiveElement element,
					const QStyleOption *option,
					QPainter *painter,
					const QWidget *widget = 0 ) const override;

	int pixelMetric( PixelMetric metric,
					const QStyleOption * option = 0,
					const QWidget * widget = 0 ) const override;

	static QPalette * s_palette;

private:
	QImage colorizeXpm( const char * const * xpm, const QBrush& fill ) const;
	void hoverColors( bool sunken, bool hover, bool active, QColor& color, QColor& blend ) const;
	QFileSystemWatcher m_styleReloader;
};


} // namespace mxm::gui

#endif // MXM_GUI_MXM_STYLE_H
