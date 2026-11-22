/*
 * StereoMatrixControls.cpp - controls for StereoMatrix effect
 *
 * Copyright (c) 2008 Paul Giblock <drfaygo/at/gmail/dot/com>
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



#include "StereoMatrixControls.h"
#include "StereoMatrix.h"
#include "ModelSettings.h"

namespace mxm
{


StereoMatrixControls::StereoMatrixControls( StereoMatrixEffect * _eff ) :
		EffectControls( _eff ),
		m_effect( _eff ),
		m_llModel( 1.0f, -1.0f, 1.0f, 0.01f, this, tr( "Left to Left" ) ),
		m_lrModel( 0.0f, -1.0f, 1.0f, 0.01f, this, tr( "Left to Right" ) ),
		m_rlModel( 0.0f, -1.0f, 1.0f, 0.01f, this, tr( "Right to Left" ) ),
		m_rrModel( 1.0f, -1.0f, 1.0f, 0.01f, this, tr( "Right to Right" ) )
{
	m_llModel.setCenterValue( 0 );
	m_lrModel.setCenterValue( 0 );
	m_rlModel.setCenterValue( 0 );
	m_rrModel.setCenterValue( 0 );

	connect( &m_llModel, SIGNAL( dataChanged() ),
			this, SLOT( changeMatrix() ) );
	connect( &m_lrModel, SIGNAL( dataChanged() ),
			this, SLOT( changeMatrix() ) );
	connect( &m_rlModel, SIGNAL( dataChanged() ),
			this, SLOT( changeMatrix() ) );
	connect( &m_rrModel, SIGNAL( dataChanged() ),
			this, SLOT( changeMatrix() ) );

	changeMatrix();
}



void StereoMatrixControls::changeMatrix()
{
}



MXM_DEFINE_MODEL_SETTINGS(StereoMatrixControls,
	visit(m_llModel, "l-l");
	visit(m_lrModel, "l-r");
	visit(m_rlModel, "r-l");
	visit(m_rrModel, "r-r");
)


} // namespace mxm