/*
 * MxmPalette.cpp - dummy class for fetching palette qproperties from CSS
 *                
 *
 * Copyright (c) 2007-2014 Vesa Kivimäki <contact/dot/diizy/at/nbl/dot/fi>
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

#include <QApplication>
#include <QStyle>
#include "MxmPalette.h"


namespace mxm::gui
{

MxmPalette::MxmPalette( QWidget * parent, QStyle * stylearg ) : 
	QWidget( parent ),
	
/*	sane defaults in case fetching from stylesheet fails*/	
	
	m_background( 36, 33, 44 ),
	m_windowText( 242, 242, 246 ),
	m_base( 19, 17, 25 ),
	m_text( 214, 212, 224 ),
	m_button( 58, 56, 70 ),
	m_shadow( 0,0,0 ),
	m_buttonText( 214, 212, 224 ),
	m_brightText( 255, 92, 160 ),
	m_highlight( 123, 63, 191 ),
	m_highlightedText( 255, 255, 255  )
{
	setStyle( stylearg );
	stylearg->polish( this );
	ensurePolished();
}

#define ACCESSMET( read, write ) \
	QColor MxmPalette:: read () const \
	{	return m_##read ; } \
	void MxmPalette:: write ( const QColor & c ) \
	{	m_##read = QColor( c ); }
	

	ACCESSMET( background, setBackground )
	ACCESSMET( windowText, setWindowText )
	ACCESSMET( base, setBase )
	ACCESSMET( text, setText )
	ACCESSMET( button, setButton )
	ACCESSMET( shadow, setShadow )
	ACCESSMET( buttonText, setButtonText )
	ACCESSMET( brightText, setBrightText )
	ACCESSMET( highlight, setHighlight )
	ACCESSMET( highlightedText, setHighlightedText )


QPalette MxmPalette::palette() const
{
	QPalette pal = QApplication::style()->standardPalette();
	
	pal.setColor( QPalette::Window, 			background() );
	pal.setColor( QPalette::WindowText, 		windowText() );	
	pal.setColor( QPalette::Base, 				base() );	
	pal.setColor( QPalette::ButtonText, 		buttonText() );	
	pal.setColor( QPalette::BrightText, 		brightText() );	
	pal.setColor( QPalette::Text, 				text() );	
	pal.setColor( QPalette::Button, 			button() );	
	pal.setColor( QPalette::Shadow, 			shadow() );	
	pal.setColor( QPalette::Highlight, 			highlight() );	
	pal.setColor( QPalette::HighlightedText, 	highlightedText() );
	return pal;
}


} // namespace mxm::gui
