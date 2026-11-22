/*
 * GranularPitchShifterControls.cpp
 *
 * Copyright (c) 2024 Lost Robot <r94231/at/gmail/dot/com>
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


#include "GranularPitchShifterControls.h"
#include "GranularPitchShifterEffect.h"
#include "ModelSettings.h"

namespace mxm
{

GranularPitchShifterControls::GranularPitchShifterControls(GranularPitchShifterEffect* effect) :
	EffectControls(effect),
	m_effect(effect),
	m_pitchModel(1.f, -48.f, 24.f, 0.01f, this, tr("Pitch")),
	m_sizeModel(10.f, 2.f, 1000.f, 0.001f, this, tr("Grain Size")),
	m_sprayModel(0.005f, 0.f, 0.5f, 0.0001f, this, tr("Spray")),
	m_jitterModel(0.f, 0.f, 1.f, 0.0001f, this, tr("Jitter")),
	m_twitchModel(0.f, 0.f, 1.f, 0.0001f, this, tr("Twitch")),
	m_pitchSpreadModel(0.f, -24.f, 24.f, 0.01f, this, tr("Pitch Stereo Spread")),
	m_spraySpreadModel(0.f, 0.f, 1.f, 0.0001f, this, tr("Spray Stereo")),
	m_shapeModel(2.f, 1.f, 2.f, 0.0001f, this, tr("Shape")),
	m_fadeLengthModel(1.f, 0.001f, 1.f, 0.00001f, this, tr("Fade Length")),
	m_feedbackModel(0.f, 0.f, 1.f, 0.00001f, this, tr("Feedback")),
	m_minLatencyModel(0.01f, 0.f, 1.f, 0.00001f, this, tr("Minimum Allowed Latency")),
	m_prefilterModel(true, this, tr("Prefilter")),
	m_densityModel(1.f, 1.f, 16.f, 0.0001f, this, tr("Density")),
	m_glideModel(0.01f, 0.f, 1.f, 0.0001f, this, tr("Glide")),
	m_rangeModel(this, tr("Ring Buffer Length"))
{
	setLogarithmicModels(m_sizeModel, m_sprayModel, m_spraySpreadModel,
		m_minLatencyModel, m_densityModel, m_glideModel);
	
	m_rangeModel.addItem(tr("5 Seconds"));
	m_rangeModel.addItem(tr("10 Seconds (Size)"));
	m_rangeModel.addItem(tr("40 Seconds (Size and Pitch)"));
	m_rangeModel.addItem(tr("40 Seconds (Size and Spray and Jitter)"));
	m_rangeModel.addItem(tr("120 Seconds (All of the above)"));
	
	connect(&m_rangeModel, &ComboBoxModel::dataChanged, this, &GranularPitchShifterControls::updateRange);
}

void GranularPitchShifterControls::updateRange()
{
	switch (m_rangeModel.value())
	{
	case 0:// 5 seconds
		m_sizeModel.setRange(4.f, 1000.f, 0.001f);
		m_pitchModel.setRange(-48.f, 24.f, 0.01f);
		m_sprayModel.setRange(0.f, 0.5f, 0.0001f);
		m_jitterModel.setRange(0.f, 1.f, 0.0001f);
		break;
	case 1:// 10 seconds (size)
		m_sizeModel.setRange(2.f, 1000.f, 0.001f);
		m_pitchModel.setRange(-48.f, 24.f, 0.01f);
		m_sprayModel.setRange(0.f, 0.5f, 0.0001f);
		m_jitterModel.setRange(0.f, 1.f, 0.0001f);
		break;
	case 2:// 40 seconds (size and pitch)
		m_sizeModel.setRange(2.f, 1000.f, 0.001f);
		m_pitchModel.setRange(-48.f, 48.f, 0.01f);
		m_sprayModel.setRange(0.f, 0.5f, 0.0001f);
		m_jitterModel.setRange(0.f, 1.f, 0.0001f);
		break;
	case 3:// 40 seconds (size and spray and jitter)
		m_sizeModel.setRange(2.f, 1000.f, 0.001f);
		m_pitchModel.setRange(-48.f, 24.f, 0.01f);
		m_sprayModel.setRange(0.f, 20.f, 0.0001f);
		m_jitterModel.setRange(0.f, 2.f, 0.0001f);
		break;
	case 4:// 120 seconds (all of the above)
		m_sizeModel.setRange(2.f, 1000.f, 0.001f);
		m_pitchModel.setRange(-48.f, 48.f, 0.01f);
		m_sprayModel.setRange(0.f, 40.f, 0.0001f);
		m_jitterModel.setRange(0.f, 2.f, 0.0001f);
		break;
	default:
		break;
	}
	m_effect->sampleRateNeedsUpdate();
}

MXM_DEFINE_MODEL_SETTINGS(GranularPitchShifterControls,
	visit(m_rangeModel, "range");
	visit(m_pitchModel, "pitch");
	visit(m_sizeModel, "size");
	visit(m_sprayModel, "spray");
	visit(m_jitterModel, "jitter");
	visit(m_twitchModel, "twitch");
	visit(m_pitchSpreadModel, "pitchSpread");
	visit(m_spraySpreadModel, "spraySpread");
	visit(m_shapeModel, "shape");
	visit(m_fadeLengthModel, "fadeLength");
	visit(m_feedbackModel, "feedback");
	visit(m_minLatencyModel, "minLatency");
	visit(m_prefilterModel, "prefilter");
	visit(m_densityModel, "density");
	visit(m_glideModel, "glide");
)


} // namespace mxm
