/*
 * eqcontrols.cpp - defination of EqControls class.
 *
 * Copyright (c) 2014 David French <dave/dot/french3/at/googlemail/dot/com>
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

#include "EqControls.h"


#include "EqControlsDialog.h"
#include "EqEffect.h"
#include "ModelSettings.h"


namespace mxm
{


EqControls::EqControls( EqEffect *effect ) :
	EffectControls( effect ),
	m_effect( effect ),
	m_inGainModel(0.f, -60.f, 20.f, 0.01f, this, tr("Input gain")),
	m_outGainModel(-0.f, -60.f, 20.f, 0.01f, this, tr("Output gain")),
	m_lowShelfGainModel( 0.f, -18, 18, 0.001f, this, tr("Low-shelf gain")),
	m_para1GainModel(0.f, -18, 18, 0.001f, this, tr("Peak 1 gain")),
	m_para2GainModel(0.f, -18, 18, 0.001f, this, tr("Peak 2 gain")),
	m_para3GainModel(0.f, -18, 18, 0.001f, this, tr("Peak 3 gain")),
	m_para4GainModel(0.f, -18, 18, 0.001f, this, tr("Peak 4 gain")),
	m_highShelfGainModel(0.f, -18, 18, 0.001f, this, tr("High-shelf gain")),
	m_hpResModel(0.707f,0.003f, 10.f, 0.001f, this, tr("HP res")),
	m_lowShelfResModel(0.707f, 0.55f, 10.f, 0.001f, this, tr("Low-shelf res")),
	m_para1BwModel(0.3f, 0.1f, 4, 0.001f, this, tr("Peak 1 BW")),
	m_para2BwModel(0.3f, 0.1f, 4, 0.001f, this, tr("Peak 2 BW")),
	m_para3BwModel(0.3f, 0.1f, 4, 0.001f, this, tr("Peak 3 BW")),
	m_para4BwModel(0.3f, 0.1f, 4, 0.001f, this, tr("Peak 4 BW")),
	m_highShelfResModel(0.707f, 0.55f, 10.f, 0.001f, this, tr("High-shelf res")),
	m_lpResModel(0.707f,0.003f, 10.f, 0.001f, this, tr("LP res")),
	m_hpFeqModel(31.f, 20.f, 20000, 0.001f, this, tr("HP freq")),
	m_lowShelfFreqModel(80.f, 20.f, 20000, 0.001f, this, tr("Low-shelf freq")),
	m_para1FreqModel(120.f, 20.f, 20000, 0.001f, this, tr("Peak 1 freq")),
	m_para2FreqModel(250.f, 20.f, 20000, 0.001f, this, tr("Peak 2 freq")),
	m_para3FreqModel(2000.f, 20.f, 20000, 0.001f, this, tr("Peak 3 freq")),
	m_para4FreqModel(4000.f, 20.f, 20000, 0.001f, this, tr("Peak 4 freq")),
	m_highShelfFreqModel(12000.f, 20.f, 20000, 0.001f, this, tr("High-shelf freq")),
	m_lpFreqModel(18000.f, 20.f, 20000, 0.001f, this, tr("LP freq")),
	m_hpActiveModel( false, this , tr( "HP active" ) ),
	m_lowShelfActiveModel( false, this , tr( "Low-shelf active" ) ),
	m_para1ActiveModel( false, this , tr( "Peak 1 active" ) ),
	m_para2ActiveModel( false, this , tr( "Peak 2 active" ) ),
	m_para3ActiveModel( false, this , tr( "Peak 3 active" ) ),
	m_para4ActiveModel( false, this , tr( "Peak 4 active" ) ),
	m_highShelfActiveModel( false, this , tr( "High-shelf active" ) ),
	m_lpActiveModel( false, this , tr( "LP active" ) ),
	m_lp12Model( false, this , tr( "LP 12" ) ),
	m_lp24Model( false, this , tr( "LP 24" ) ),
	m_lp48Model( false, this , tr( "LP 48" ) ),
	m_hp12Model( false, this , tr( "HP 12" ) ),
	m_hp24Model( false, this , tr( "HP 24" ) ),
	m_hp48Model( false, this , tr( "HP 48" ) ),
	m_lpTypeModel( 0,0,2, this, tr( "Low-pass type" ) ) ,
	m_hpTypeModel( 0,0,2, this, tr( "High-pass type" ) ),
	m_analyseInModel( true, this , tr( "Analyse IN" ) ),
	m_analyseOutModel( true, this, tr( "Analyse OUT" ) )
{
	setLogarithmicModels(m_hpFeqModel, m_lowShelfFreqModel, m_para1FreqModel,
		m_para2FreqModel, m_para3FreqModel, m_para4FreqModel, m_highShelfFreqModel,
		m_lpFreqModel, m_para1GainModel, m_inGainModel);
	m_inPeakL = 0;
	m_inPeakR = 0;
	m_outPeakL = 0;
	m_outPeakR = 0;
	m_lowShelfPeakL = 0; m_lowShelfPeakR = 0;
	m_para1PeakL = 0; m_para1PeakR = 0;
	m_para2PeakL = 0; m_para2PeakR = 0;
	m_para3PeakL = 0; m_para3PeakR = 0;
	m_para4PeakL = 0; m_para4PeakR = 0;
	m_highShelfPeakL = 0; m_highShelfPeakR = 0;
	m_inProgress = false;
}




MXM_DEFINE_MODEL_SETTINGS(EqControls,
	visit(m_inGainModel, "Inputgain");
	visit(m_outGainModel, "Outputgain");
	visit(m_lowShelfGainModel, "Lowshelfgain");
	visit(m_para1GainModel, "Peak1gain");
	visit(m_para2GainModel, "Peak2gain");
	visit(m_para3GainModel, "Peak3gain");
	visit(m_para4GainModel, "Peak4gain");
	visit(m_highShelfGainModel, "HighShelfgain");
	visit(m_hpResModel, "HPres");
	visit(m_lowShelfResModel, "LowShelfres");
	visit(m_para1BwModel, "Peak1bw");
	visit(m_para2BwModel, "Peak2bw");
	visit(m_para3BwModel, "Peak3bw");
	visit(m_para4BwModel, "Peak4bw");
	visit(m_highShelfResModel, "HighShelfres");
	visit(m_lpResModel, "LPres");
	visit(m_hpFeqModel, "HPfreq");
	visit(m_lowShelfFreqModel, "LowShelffreq");
	visit(m_para1FreqModel, "Peak1freq");
	visit(m_para2FreqModel, "Peak2freq");
	visit(m_para3FreqModel, "Peak3freq");
	visit(m_para4FreqModel, "Peak4freq");
	visit(m_highShelfFreqModel, "Highshelffreq");
	visit(m_lpFreqModel, "LPfreq");
	visit(m_hpActiveModel, "HPactive");
	visit(m_lowShelfActiveModel, "Lowshelfactive");
	visit(m_para1ActiveModel, "Peak1active");
	visit(m_para2ActiveModel, "Peak2active");
	visit(m_para3ActiveModel, "Peak3active");
	visit(m_para4ActiveModel, "Peak4active");
	visit(m_highShelfActiveModel, "Highshelfactive");
	visit(m_lpActiveModel, "LPactive");
	visit(m_lp12Model, "LP12");
	visit(m_lp24Model, "LP24");
	visit(m_lp48Model, "LP48");
	visit(m_hp12Model, "HP12");
	visit(m_hp24Model, "HP24");
	visit(m_hp48Model, "HP48");
	visit(m_lpTypeModel, "LP");
	visit(m_hpTypeModel, "HP");
	visit(m_analyseInModel, "AnalyseIn");
	visit(m_analyseOutModel, "AnalyseOut");
)

gui::EffectControlDialog* EqControls::createView()
{
	return new gui::EqControlsDialog( this );
}


} // namespace mxm
