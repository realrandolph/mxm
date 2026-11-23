/*
 * FrequencyShifterControls.cpp
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
 */

#include "FrequencyShifterEffect.h"
#include "FrequencyShifterControls.h"

#include <QDomElement>
#include "ModelSettings.h"

namespace mxm
{

FrequencyShifterControls::FrequencyShifterControls(FrequencyShifterEffect* e) :
	EffectControls(e),
	m_effect(e),
	m_mix(1.f, 0.f, 1.f, 0.01f, this, "Mix"),
	m_freqShift(0.f, -20000.f, 20000.f, 0.001f, this, "Frequency Shift"),
	m_spreadShift(0.f, -50.f, 50.f, 0.01f, this, "Spread Shift"),
	m_ring(0.f, 0.f, 1.f, 0.01f, this, "Ring"),
	m_feedback(0.f, 0.f, 1.f, 0.001f, this, "Feedback"),
	m_delayLengthLong(0.f, 0.f, 2000.f, 0.001f, this, "Delay Length"),
	m_delayLengthShort(0.f, 0.f, 20.f, 0.001f, this, "Fine Delay Length"),
	m_delayDamp(22000.f, 100.f, 22000.f, 0.1f, this, "Delay Damping"),
	m_delayGlide(0.05f, 0.f, 1.f, 0.0001f, this, "Delay Glide"),
	m_lfoAmount(0.f, 0.f, 2000.f, 0.001f, this, "LFO Amount"),
	m_lfoRate(0.2f, 0.f, 200.f, 0.001f, this, "LFO Rate"),
	m_lfoStereoPhase(0.f, 0.f, 1.f, 0.001f, this, "LFO StereoPhase"),
	m_glide(0.05f, 0.f, 1.f, 0.0001f, this, "Glide"),
	m_tone(22000.f, 100.f, 22000.f, 0.1f, this, "Tone"),
	m_phase(0.f, -2.f, 2.f, 0.00001f, this, "Phase"),
	m_antireflect(false, this, "Antireflect"),
	m_routeMode(0, 0, 2, this, "Route Mode"),
	m_harmonics(0.f, 0.f, 1.f, 0.0001f, this, "Harmonics"),
	m_resetShifter(false, this, "Reset Shifter"),
	m_resetLfo(false, this, "Reset LFO")
{
	setLogarithmicModels(m_spreadShift, m_delayLengthLong, m_delayLengthShort, m_delayDamp,
		m_delayGlide, m_lfoAmount, m_lfoRate, m_glide, m_tone);
}

MXM_DEFINE_MODEL_SETTINGS(FrequencyShifterControls,
	visit(m_mix, "mix");
	visit(m_freqShift, "freqShift");
	visit(m_spreadShift, "spreadShift");
	visit(m_ring, "ring");
	visit(m_feedback, "feedback");
	visit(m_delayLengthLong, "m_delayLengthLong");
	visit(m_delayLengthShort, "delayLengthShort");
	visit(m_delayDamp, "delayDamp");
	visit(m_delayGlide, "delayGlide");
	visit(m_lfoAmount, "lfoAmount");
	visit(m_lfoRate, "lfoRate");
	visit(m_lfoStereoPhase, "lfoStereoPhase");
	visit(m_antireflect, "antireflect");
	visit(m_routeMode, "routeMode");
	visit(m_harmonics, "harmonics");
	visit(m_glide, "glide");
	visit(m_tone, "tone");
	visit(m_phase, "phase");
)

} // namespace mxm

