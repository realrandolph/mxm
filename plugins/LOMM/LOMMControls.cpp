/*
 * LOMMControls.cpp
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


#include "LOMMControls.h"
#include "LOMM.h"

#include <QMessageBox>
#include "ModelSettings.h"

namespace mxm
{

LOMMControls::LOMMControls(LOMMEffect* effect) :
	EffectControls(effect),
	m_effect(effect),
	m_depthModel(0.4f, 0, 1, 0.00001f, this, tr("Depth")),
	m_timeModel(1, 0, 10, 0.00001f, this, tr("Time")),
	m_inVolModel(0, -48, 48, 0.00001f, this, tr("Input Volume")),
	m_outVolModel(8, -48, 48, 0.00001f, this, tr("Output Volume")),
	m_upwardModel(1, 0, 2, 0.00001f, this, tr("Upward Depth")),
	m_downwardModel(1, 0, 2, 0.00001f, this, tr("Downward Depth")),
	m_split1Model(2500, 20, 20000, 0.01f, this, tr("High/Mid Split")),
	m_split2Model(88.3f, 20, 20000, 0.01f, this, tr("Mid/Low Split")),
	m_split1EnabledModel(true, this, tr("Enable High/Mid Split")),
	m_split2EnabledModel(true, this, tr("Enable Mid/Low Split")),
	m_band1EnabledModel(true, this, tr("Enable High Band")),
	m_band2EnabledModel(true, this, tr("Enable Mid Band")),
	m_band3EnabledModel(true, this, tr("Enable Low Band")),
	m_inHighModel(0, -48, 48, 0.00001f, this, tr("High Input Volume")),
	m_inMidModel(0, -48, 48, 0.00001f, this, tr("Mid Input Volume")),
	m_inLowModel(0, -48, 48, 0.00001f, this, tr("Low Input Volume")),
	m_outHighModel(4.6f, -48, 48, 0.00001f, this, tr("High Output Volume")),
	m_outMidModel(0.f, -48, 48, 0.00001f, this, tr("Mid Output Volume")),
	m_outLowModel(4.6f, -48, 48, 0.00001f, this, tr("Low Output Volume")),
	m_aThreshHModel(-30.3f, LOMM_DISPLAY_MIN, LOMM_DISPLAY_MAX, 0.001f, this, tr("Above Threshold High")),
	m_aThreshMModel(-25.f, LOMM_DISPLAY_MIN, LOMM_DISPLAY_MAX, 0.001f, this, tr("Above Threshold Mid")),
	m_aThreshLModel(-28.6f, LOMM_DISPLAY_MIN, LOMM_DISPLAY_MAX, 0.001f, this, tr("Above Threshold Low")),
	m_aRatioHModel(99.99f, 1, 99.99f, 0.01f, this, tr("Above Ratio High")),
	m_aRatioMModel(66.7f, 1, 99.99f, 0.01f, this, tr("Above Ratio Mid")),
	m_aRatioLModel(66.7f, 1, 99.99f, 0.01f, this, tr("Above Ratio Low")),
	m_bThreshHModel(-35.6f, LOMM_DISPLAY_MIN, LOMM_DISPLAY_MAX, 0.001f, this, tr("Below Threshold High")),
	m_bThreshMModel(-36.6f, LOMM_DISPLAY_MIN, LOMM_DISPLAY_MAX, 0.001f, this, tr("Below Threshold Mid")),
	m_bThreshLModel(-35.6f, LOMM_DISPLAY_MIN, LOMM_DISPLAY_MAX, 0.001f, this, tr("Below Threshold Low")),
	m_bRatioHModel(4.17f, 1, 99.99f, 0.01f, this, tr("Below Ratio High")),
	m_bRatioMModel(4.17f, 1, 99.99f, 0.01f, this, tr("Below Ratio Mid")),
	m_bRatioLModel(4.17f, 1, 99.99f, 0.01f, this, tr("Below Ratio Low")),
	m_atkHModel(13.5f, 0, 1000, 0.001f, this, tr("Attack High")),
	m_atkMModel(22.4f, 0, 1000, 0.001f, this, tr("Attack Mid")),
	m_atkLModel(47.8f, 0, 1000, 0.001f, this, tr("Attack Low")),
	m_relHModel(132, 0, 1000, 0.001f, this, tr("Release High")),
	m_relMModel(282, 0, 1000, 0.001f, this, tr("Release Mid")),
	m_relLModel(282, 0, 1000, 0.001f, this, tr("Release Low")),
	m_rmsTimeModel(10, 0, 500, 0.001f, this, tr("RMS Time")),
	m_kneeModel(6, 0, 36, 0.00001f, this, tr("Knee")),
	m_rangeModel(36, 0, 96, 0.00001f, this, tr("Range")),
	m_balanceModel(0, -18, 18, 0.00001f, this, tr("Balance")),
	m_depthScalingModel(true, this, tr("Scale output volume with Depth")),
	m_stereoLinkModel(false, this, tr("Stereo Link")),
	m_autoTimeModel(0, 0, 1, 0.00001f, this, tr("Auto Time")),
	m_mixModel(1, 0, 1, 0.00001f, this, tr("Mix")),
	m_feedbackModel(false, this, tr("Feedback")),
	m_midsideModel(false, this, tr("Mid/Side")),
	m_lookaheadEnableModel(false, this, tr("Lookahead")),
	m_lookaheadModel(0.f, 0.f, LOMM_MAX_LOOKAHEAD, 0.01f, this, tr("Lookahead Length")),
	m_lowSideUpwardSuppressModel(false, this, tr("Suppress upward compression for side band"))
{
	auto models = {&m_timeModel, &m_inVolModel, &m_outVolModel, &m_inHighModel, &m_inMidModel,
		&m_inLowModel, &m_outHighModel, &m_outMidModel, &m_outLowModel, &m_aRatioHModel,
		&m_aRatioMModel, &m_aRatioLModel, &m_bRatioHModel, &m_bRatioMModel, &m_bRatioLModel,
		&m_atkHModel, &m_atkMModel, &m_atkLModel, &m_relHModel, &m_relMModel, &m_relLModel,
		&m_rmsTimeModel, &m_balanceModel};
	for (auto model : models) { model->setScaleLogarithmic(true); }
}


void LOMMControls::resetAllParameters()
{
	int choice = QMessageBox::question(m_view, "Clear Plugin Settings", "Are you sure you want to clear all parameters?\n(This wipes LOMM to a clean slate, not the default preset.)", QMessageBox::Yes | QMessageBox::No);
	if (choice != QMessageBox::Yes) { return; }
	
	// give the user a chance to beg MXM for forgiveness
	addJournalCheckPoint();
	
	// This plugin's normal default values are fairly close to what they'd want in most applications.
	// The Init button is there so the user can start from a clean slate instead.
	// These are those values.
	setInitAndReset(m_depthModel, 1);
	setInitAndReset(m_timeModel, 1);
	setInitAndReset(m_inVolModel, 0);
	setInitAndReset(m_outVolModel, 0);
	setInitAndReset(m_upwardModel, 1);
	setInitAndReset(m_downwardModel, 1);
	setInitAndReset(m_split1Model, 2500);
	setInitAndReset(m_split2Model, 88);
	setInitAndReset(m_split1EnabledModel, true);
	setInitAndReset(m_split2EnabledModel, true);
	setInitAndReset(m_band1EnabledModel, true);
	setInitAndReset(m_band2EnabledModel, true);
	setInitAndReset(m_band3EnabledModel, true);
	setInitAndReset(m_inHighModel, 0);
	setInitAndReset(m_inMidModel, 0);
	setInitAndReset(m_inLowModel, 0);
	setInitAndReset(m_outHighModel, 0);
	setInitAndReset(m_outMidModel, 0);
	setInitAndReset(m_outLowModel, 0);
	setInitAndReset(m_aThreshHModel, m_aThreshHModel.maxValue());
	setInitAndReset(m_aThreshMModel, m_aThreshMModel.maxValue());
	setInitAndReset(m_aThreshLModel, m_aThreshLModel.maxValue());
	setInitAndReset(m_aRatioHModel, 1);
	setInitAndReset(m_aRatioMModel, 1);
	setInitAndReset(m_aRatioLModel, 1);
	setInitAndReset(m_bThreshHModel, m_bThreshHModel.minValue());
	setInitAndReset(m_bThreshMModel, m_bThreshMModel.minValue());
	setInitAndReset(m_bThreshLModel, m_bThreshLModel.minValue());
	setInitAndReset(m_bRatioHModel, 1);
	setInitAndReset(m_bRatioMModel, 1);
	setInitAndReset(m_bRatioLModel, 1);
	setInitAndReset(m_atkHModel, 13.5);
	setInitAndReset(m_atkMModel, 22.4);
	setInitAndReset(m_atkLModel, 47.8);
	setInitAndReset(m_relHModel, 132);
	setInitAndReset(m_relMModel, 282);
	setInitAndReset(m_relLModel, 282);
	setInitAndReset(m_rmsTimeModel, 10);
	setInitAndReset(m_kneeModel, 6);
	setInitAndReset(m_rangeModel, 36);
	setInitAndReset(m_balanceModel, 0);
	setInitAndReset(m_depthScalingModel, true);
	setInitAndReset(m_stereoLinkModel, false);
	setInitAndReset(m_autoTimeModel, 0);
	setInitAndReset(m_mixModel, 1);
	setInitAndReset(m_feedbackModel, false);
	setInitAndReset(m_midsideModel, false);
	setInitAndReset(m_lookaheadEnableModel, false);
	setInitAndReset(m_lookaheadModel, 0.f);
	setInitAndReset(m_lowSideUpwardSuppressModel, false);
}



MXM_DEFINE_MODEL_SETTINGS(LOMMControls,
	visit(m_depthModel, "depth");
	visit(m_timeModel, "time");
	visit(m_inVolModel, "inVol");
	visit(m_outVolModel, "outVol");
	visit(m_upwardModel, "upward");
	visit(m_downwardModel, "downward");
	visit(m_split1Model, "split1");
	visit(m_split2Model, "split2");
	visit(m_split1EnabledModel, "split1Enabled");
	visit(m_split2EnabledModel, "split2Enabled");
	visit(m_band1EnabledModel, "band1Enabled");
	visit(m_band2EnabledModel, "band2Enabled");
	visit(m_band3EnabledModel, "band3Enabled");
	visit(m_inHighModel, "inHigh");
	visit(m_inMidModel, "inMid");
	visit(m_inLowModel, "inLow");
	visit(m_outHighModel, "outHigh");
	visit(m_outMidModel, "outMid");
	visit(m_outLowModel, "outLow");
	visit(m_aThreshHModel, "aThreshH");
	visit(m_aThreshMModel, "aThreshM");
	visit(m_aThreshLModel, "aThreshL");
	visit(m_aRatioHModel, "aRatioH");
	visit(m_aRatioMModel, "aRatioM");
	visit(m_aRatioLModel, "aRatioL");
	visit(m_bThreshHModel, "bThreshH");
	visit(m_bThreshMModel, "bThreshM");
	visit(m_bThreshLModel, "bThreshL");
	visit(m_bRatioHModel, "bRatioH");
	visit(m_bRatioMModel, "bRatioM");
	visit(m_bRatioLModel, "bRatioL");
	visit(m_atkHModel, "atkH");
	visit(m_atkMModel, "atkM");
	visit(m_atkLModel, "atkL");
	visit(m_relHModel, "relH");
	visit(m_relMModel, "relM");
	visit(m_relLModel, "relL");
	visit(m_rmsTimeModel, "rmsTime");
	visit(m_kneeModel, "knee");
	visit(m_rangeModel, "range");
	visit(m_balanceModel, "balance");
	visit(m_depthScalingModel, "depthScaling");
	visit(m_stereoLinkModel, "stereoLink");
	visit(m_autoTimeModel, "autoTime");
	visit(m_mixModel, "mix");
	visit(m_feedbackModel, "feedback");
	visit(m_midsideModel, "midside");
	visit(m_lookaheadEnableModel, "lookaheadEnable");
	visit(m_lookaheadModel, "lookahead");
	visit(m_lowSideUpwardSuppressModel, "lowSideUpwardSuppress");
)


} // namespace mxm


