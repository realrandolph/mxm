/*
 * DispersionControls.cpp
 *
 * Copyright (c) 2023 Lost Robot <r94231/at/gmail/dot/com>
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


#include "DispersionControls.h"
#include "Dispersion.h"
#include "ModelSettings.h"


namespace mxm
{

DispersionControls::DispersionControls(DispersionEffect* effect) :
	EffectControls(effect),
	m_effect(effect),
	m_amountModel(0, 0, MAX_DISPERSION_FILTERS, this, tr("Amount")),
	m_freqModel(200, 20, 20000, 0.001f, this, tr("Frequency")),
	m_resoModel(0.707f, 0.01f, 8, 0.0001f, this, tr("Resonance")),
	m_feedbackModel(0.f, -1.f, 1.f, 0.0001f, this, tr("Feedback")),
	m_dcModel(false, this, tr("DC Offset Removal"))
{
	m_freqModel.setScaleLogarithmic(true);
	m_resoModel.setScaleLogarithmic(true);
}



MXM_DEFINE_MODEL_SETTINGS(DispersionControls,
	visit(m_amountModel, "amount");
	visit(m_freqModel, "freq");
	visit(m_resoModel, "reso");
	visit(m_feedbackModel, "feedback");
	visit(m_dcModel, "dc");
)


} // namespace mxm


