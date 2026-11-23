/*
 * CompressorControls.cpp
 *
 * Copyright (c) 2020 Lost Robot <r94231@gmail.com>
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


#include "CompressorControls.h"
#include "Compressor.h"
#include "ModelSettings.h"



namespace mxm
{

CompressorControls::CompressorControls(CompressorEffect* effect) :
	EffectControls(effect),
	m_effect(effect),
	m_thresholdModel(-8.0f, -60.0f, 0.0f, 0.001f, this, tr("Threshold")),
	m_ratioModel(1.8f, 1.0f, 20.0f, 0.001f, this, tr("Ratio")),
	m_attackModel(10.0f, 0.005f, 250.f, 0.001f, this, tr("Attack")),
	m_releaseModel(100.0f, 1.f, 2500.f, 0.001f, this, tr("Release")),
	m_kneeModel(12.0f, 0.0f, 96.0f, 0.01f, this, tr("Knee")),
	m_holdModel(0.0f, 0.0f, 500.0f, 0.01f, this, tr("Hold")),
	m_rangeModel(-240.0f, -240.0f, 0.0f, 0.01f, this, tr("Range")),
	m_rmsModel(1.0f, 0.0f, 250.0f, 0.01f, this, tr("RMS Size")),
	m_midsideModel(0.0f, 0.0f, 1.0f, this, tr("Mid/Side")),
	m_peakmodeModel(0.0f, 0.0f, 1.0f, this, tr("Peak Mode")),
	m_lookaheadLengthModel(0.0f, 0.0f, 20.0f, 0.0001f, this, tr("Lookahead Length")),
	m_inBalanceModel(0.0f, -1.0f, 1.0f, 0.0001f, this, tr("Input Balance")),
	m_outBalanceModel(0.0f, -1.0f, 1.0f, 0.0001f, this, tr("Output Balance")),
	m_limiterModel(0.f, 0.f, 1.0f, this, tr("Limiter")),
	m_outGainModel(0.f, -60.f, 30.f, 0.01f, this, tr("Output Gain")),
	m_inGainModel(0.f, -60.f, 30.f, 0.01f, this, tr("Input Gain")),
	m_blendModel(1.f, 0.f, 3.f, 0.0001f, this, tr("Blend")),
	m_stereoBalanceModel(0.0f, -1.0f, 1.0f, 0.0001f, this, tr("Stereo Balance")),
	m_autoMakeupModel(false, this, tr("Auto Makeup Gain")),
	m_auditionModel(false, this, tr("Audition")),
	m_feedbackModel(false, this, tr("Feedback")),
	m_autoAttackModel(0.0f, 0.f, 100.0f, 0.01f, this, tr("Auto Attack")),
	m_autoReleaseModel(0.0f, 0.f, 100.0f, 0.01f, this, tr("Auto Release")),
	m_lookaheadModel(false, this, tr("Lookahead")),
	m_tiltModel(0.0f, -6.0f, 6.0f, 0.0001f, this, tr("Tilt")),
	m_tiltFreqModel(150.0f, 20.0f, 20000.0f, 0.1f, this, tr("Tilt Frequency")),
	m_stereoLinkModel(1.0f, 0.0f, 4.0f, this, tr("Stereo Link")),
	m_mixModel(100.0f, 0.f, 100.0f, 0.01f, this, tr("Mix"))
{
	setLogarithmicModels(m_ratioModel, m_holdModel, m_attackModel, m_releaseModel,
		m_thresholdModel, m_rangeModel, m_lookaheadLengthModel, m_rmsModel,
		m_kneeModel, m_tiltFreqModel);
}


MXM_DEFINE_MODEL_SETTINGS(CompressorControls,
	visit(m_thresholdModel, "threshold");
	visit(m_ratioModel, "ratio");
	visit(m_attackModel, "attack");
	visit(m_releaseModel, "release");
	visit(m_kneeModel, "knee");
	visit(m_holdModel, "hold");
	visit(m_rangeModel, "range");
	visit(m_rmsModel, "rms");
	visit(m_midsideModel, "midside");
	visit(m_peakmodeModel, "peakmode");
	visit(m_lookaheadLengthModel, "lookaheadLength");
	visit(m_inBalanceModel, "inBalance");
	visit(m_outBalanceModel, "outBalance");
	visit(m_limiterModel, "limiter");
	visit(m_outGainModel, "outGain");
	visit(m_inGainModel, "inGain");
	visit(m_blendModel, "blend");
	visit(m_stereoBalanceModel, "stereoBalance");
	visit(m_autoMakeupModel, "autoMakeup");
	visit(m_auditionModel, "audition");
	visit(m_feedbackModel, "feedback");
	visit(m_autoAttackModel, "autoAttack");
	visit(m_autoReleaseModel, "autoRelease");
	visit(m_lookaheadModel, "lookahead");
	visit(m_tiltModel, "tilt");
	visit(m_tiltFreqModel, "tiltFreq");
	visit(m_stereoLinkModel, "stereoLink");
	visit(m_mixModel, "mix");
)


} // namespace mxm