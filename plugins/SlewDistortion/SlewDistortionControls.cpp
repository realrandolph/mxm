/*
 * SlewDistortionControls.cpp
 *
 * Copyright (c) 2025 Lost Robot <r94231/at/gmail/dot/com>
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

#include "SlewDistortionControls.h"

#include <QDomElement>

#include "SlewDistortion.h"
#include "ModelSettings.h"

namespace mxm
{

SlewDistortionControls::SlewDistortionControls(SlewDistortion* effect) :
	EffectControls(effect),
	m_effect(effect),
	m_distType1Model(this, tr("Type 1")),
	m_distType2Model(this, tr("Type 2")),
	m_drive1Model(0.0f, -24.f, 24.0f, 0.0001f, this, tr("Drive 1")),
	m_drive2Model(0.0f, -24.f, 24.0f, 0.0001f, this, tr("Drive 2")),
	m_slewUp1Model(6.0f, -96.f, 6.0f, 0.0001f, this, tr("Slew Up 1")),
	m_slewUp2Model(6.0f, -96.f, 6.0f, 0.0001f, this, tr("Slew Up 2")),
	m_slewDown1Model(6.0f, -96.f, 6.0f, 0.0001f, this, tr("Slew Down 1")),
	m_slewDown2Model(6.0f, -96.f, 6.0f, 0.0001f, this, tr("Slew Down 2")),
	m_bias1Model(0.0f, -2.0f, 2.0f, 0.0001f, this, tr("Bias 1")),
	m_bias2Model(0.0f, -2.0f, 2.0f, 0.0001f, this, tr("Bias 2")),
	m_warp1Model(0.0f, 0.0f, 0.99f, 0.0001f, this, tr("Warp 1")),
	m_warp2Model(0.0f, 0.0f, 0.99f, 0.0001f, this, tr("Warp 2")),
	m_crush1Model(0.0f, 0.0f, 24.0f, 0.0001f, this, tr("Crush 1")),
	m_crush2Model(0.0f, 0.0f, 24.0f, 0.0001f, this, tr("Crush 2")),
	m_outVol1Model(0.0f, -24.0f, 24.0f, 0.0001f, this, tr("Out Vol 1")),
	m_outVol2Model(0.0f, -24.0f, 24.0f, 0.0001f, this, tr("Out Vol 2")),
	m_attack1Model(2.0f, 0.01f, 200.0f, 0.01f, this, tr("Attack 1")),
	m_attack2Model(2.0f, 0.01f, 200.0f, 0.01f, this, tr("Attack 2")),
	m_release1Model(20.0f, 0.01f, 800.0f, 0.01f, this, tr("Release 1")),
	m_release2Model(20.0f, 0.01f, 800.0f, 0.01f, this, tr("Release 2")),
	m_dynamics1Model(0.0f, 0.0f, 1.0f, 0.0001f, this, tr("Dynamics 1")),
	m_dynamics2Model(0.0f, 0.0f, 1.0f, 0.0001f, this, tr("Dynamics 2")),
	m_dynamicSlew1Model(0.0f, -8.0f, 8.0f, 0.0001f, this, tr("Dynamic Slew 1")),
	m_dynamicSlew2Model(0.0f, -8.0f, 8.0f, 0.0001f, this, tr("Dynamic Slew 2")),
	m_dcRemoveModel(true, this, tr("DC Offset Remover")),
	m_multibandModel(false, this, tr("Multiband")),
	m_oversamplingModel(0, 0, SLEWDIST_MAX_OVERSAMPLE_STAGES, this, tr("Oversample")),
	m_splitModel(200.0f, 100.0f, 20000.0f, 0.1f, this, tr("Split")),
	m_mix1Model(1.0f, 0.0f, 1.0f, 0.0001f, this, tr("Mix 1")),
	m_mix2Model(1.0f, 0.0f, 1.0f, 0.0001f, this, tr("Mix 2")),
	m_slewLink1Model(true, this, tr("Slew Link 1")),
	m_slewLink2Model(true, this, tr("Slew Link 2"))
{
	setLogarithmicModels(m_slewUp1Model, m_slewUp2Model, m_slewDown1Model, m_slewDown2Model,
		m_crush1Model, m_crush2Model, m_attack1Model, m_attack2Model, m_release1Model,
		m_release2Model, m_dynamicSlew1Model, m_dynamicSlew2Model, m_splitModel);
	
	m_distType1Model.addItem(tr("Hard Clip"));
	m_distType1Model.addItem(tr("Tanh"));
	m_distType1Model.addItem(tr("Fast Soft Clip 1"));
	m_distType1Model.addItem(tr("Fast Soft Clip 2"));
	m_distType1Model.addItem(tr("Sinusoidal"));
	m_distType1Model.addItem(tr("Foldback"));
	m_distType1Model.addItem(tr("Full Rectify"));
	m_distType1Model.addItem(tr("Half Rectify"));
	m_distType1Model.addItem(tr("Smooth Rectify"));
	m_distType1Model.addItem(tr("Bitcrush"));
	
	m_distType2Model.addItem(tr("Hard Clip"));
	m_distType2Model.addItem(tr("Tanh"));
	m_distType2Model.addItem(tr("Fast Soft Clip 1"));
	m_distType2Model.addItem(tr("Fast Soft Clip 2"));
	m_distType2Model.addItem(tr("Sinusoidal"));
	m_distType2Model.addItem(tr("Foldback"));
	m_distType2Model.addItem(tr("Full Rectify"));
	m_distType2Model.addItem(tr("Half Rectify"));
	m_distType2Model.addItem(tr("Smooth Rectify"));
	m_distType2Model.addItem(tr("Bitcrush"));
}


MXM_DEFINE_MODEL_SETTINGS(SlewDistortionControls,
	visit(m_distType1Model, "distType1");
	visit(m_distType2Model, "distType2");
	visit(m_drive1Model, "drive1");
	visit(m_drive2Model, "drive2");
	visit(m_slewUp1Model, "slewUp1");
	visit(m_slewUp2Model, "slewUp2");
	visit(m_slewDown1Model, "slewDown1");
	visit(m_slewDown2Model, "slewDown2");
	visit(m_bias1Model, "bias1");
	visit(m_bias2Model, "bias2");
	visit(m_warp1Model, "warp1");
	visit(m_warp2Model, "warp2");
	visit(m_crush1Model, "crush1");
	visit(m_crush2Model, "crush2");
	visit(m_outVol1Model, "outVol1");
	visit(m_outVol2Model, "outVol2");
	visit(m_attack1Model, "attack1");
	visit(m_attack2Model, "attack2");
	visit(m_release1Model, "release1");
	visit(m_release2Model, "release2");
	visit(m_dynamics1Model, "dynamics1");
	visit(m_dynamics2Model, "dynamics2");
	visit(m_dynamicSlew1Model, "dynamicSlew1");
	visit(m_dynamicSlew2Model, "dynamicSlew2");
	visit(m_dcRemoveModel, "dcRemove");
	visit(m_multibandModel, "multiband");
	visit(m_oversamplingModel, "oversampling");
	visit(m_splitModel, "split");
	visit(m_mix1Model, "mix1");
	visit(m_mix2Model, "mix2");
	visit(m_slewLink1Model, "slewLink1");
	visit(m_slewLink2Model, "slewLink2");
)



} // namespace mxm
