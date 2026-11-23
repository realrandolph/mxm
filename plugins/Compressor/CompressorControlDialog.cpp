/*
 * CompressorControlDialog.cpp
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

#include "Compressor.h"
#include "CompressorControlDialog.h"
#include "CompressorControls.h"

#include <cmath>
#include <string>
#include <QLabel>
#include <QPainter>
#include <QWheelEvent>

#include "AutomatableButton.h"
#include "embed.h"
#include "../Eq/EqFader.h"
#include "GuiApplication.h"
#include "Knob.h"
#include "MainWindow.h"
#include "PixmapButton.h"

namespace mxm::gui
{


CompressorControlDialog::CompressorControlDialog(CompressorControls* controls) :
	EffectControlDialog(controls),
	m_controls(controls)
{
	setAutoFillBackground(false);
	setAttribute(Qt::WA_NoSystemBackground, true);
	
	setMinimumSize(MIN_COMP_SCREEN_X, MIN_COMP_SCREEN_Y);
	resize(COMP_SCREEN_X, COMP_SCREEN_Y);

	m_graphPixmap.fill(QColor("transparent"));

	m_visPixmap.fill(QColor("transparent"));

	m_kneePixmap.fill(QColor("transparent"));

	m_kneePixmap2.fill(QColor("transparent"));

	m_miscPixmap.fill(QColor("transparent"));

	m_controlsBoxLabel = new QLabel(this);
	m_controlsBoxLabel->setPixmap(PLUGIN_NAME::getIconPixmap("controlsBox"));
	m_controlsBoxLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

	m_rmsEnabledLabel = new QLabel(this);
	m_rmsEnabledLabel->setPixmap(PLUGIN_NAME::getIconPixmap("knob_enabled"));
	m_rmsEnabledLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

	m_blendEnabledLabel = new QLabel(this);
	m_blendEnabledLabel->setPixmap(PLUGIN_NAME::getIconPixmap("knob_enabled"));
	m_blendEnabledLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

	m_lookaheadEnabledLabel = new QLabel(this);
	m_lookaheadEnabledLabel->setPixmap(PLUGIN_NAME::getIconPixmap("knob_enabled"));
	m_lookaheadEnabledLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

	m_ratioEnabledLabel = new QLabel(this);
	m_ratioEnabledLabel->setPixmap(PLUGIN_NAME::getIconPixmap("knob_enabled_large"));
	m_ratioEnabledLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto makeKnob = [this](bool large, const QString& hint, const QString& unit,
		FloatModel* model, const QString& tooltip) {
		auto* knob = new Knob(KnobType::Styled, this);
		if (large) { makeLargeKnob(knob, hint, unit); }
		else { makeSmallKnob(knob, hint, unit); }
		knob->setModel(model);
		knob->setToolTip(tooltip);
		return knob;
	};

	m_thresholdKnob = makeKnob(true, tr("Threshold:"), " dBFS", &controls->m_thresholdModel,
		tr("Volume at which the compression begins to take place"));
	m_ratioKnob = makeKnob(true, tr("Ratio:"), ":1", &controls->m_ratioModel,
		tr("How far the compressor must turn the volume down after crossing the threshold"));
	m_attackKnob = makeKnob(true, tr("Attack:"), " ms", &controls->m_attackModel,
		tr("Speed at which the compressor starts to compress the audio"));
	m_releaseKnob = makeKnob(true, tr("Release:"), " ms", &controls->m_releaseModel,
		tr("Speed at which the compressor ceases to compress the audio"));
	m_kneeKnob = makeKnob(false, tr("Knee:"), " dB", &controls->m_kneeModel,
		tr("Smooth out the gain reduction curve around the threshold"));
	m_rangeKnob = makeKnob(false, tr("Range:"), " dBFS", &controls->m_rangeModel,
		tr("Maximum gain reduction"));
	m_lookaheadLengthKnob = makeKnob(false, tr("Lookahead Length:"), " ms", &controls->m_lookaheadLengthModel,
		tr("How long the compressor has to react to the sidechain signal ahead of time"));
	m_holdKnob = makeKnob(false, tr("Hold:"), " ms", &controls->m_holdModel,
		tr("Delay between attack and release stages"));
	m_rmsKnob = makeKnob(false, tr("RMS Size:"), "", &controls->m_rmsModel,
		tr("Size of the RMS buffer"));
	m_inBalanceKnob = makeKnob(false, tr("Input Balance:"), "", &controls->m_inBalanceModel,
		tr("Bias the input audio to the left/right or mid/side"));
	m_outBalanceKnob = makeKnob(false, tr("Output Balance:"), "", &controls->m_outBalanceModel,
		tr("Bias the output audio to the left/right or mid/side"));
	m_stereoBalanceKnob = makeKnob(false, tr("Stereo Balance:"), "", &controls->m_stereoBalanceModel,
		tr("Bias the sidechain signal to the left/right or mid/side"));
	m_blendKnob = makeKnob(false, tr("Stereo Link Blend:"), "", &controls->m_blendModel,
		tr("Blend between unlinked/maximum/average/minimum stereo linking modes"));
	m_tiltKnob = makeKnob(false, tr("Tilt Gain:"), " dB", &controls->m_tiltModel,
		tr("Bias the sidechain signal to the low or high frequencies.  -6 db is lowpass, 6 db is highpass."));
	m_tiltFreqKnob = makeKnob(false, tr("Tilt Frequency:"), " Hz", &controls->m_tiltFreqModel,
		tr("Center frequency of sidechain tilt filter"));
	m_mixKnob = makeKnob(false, tr("Mix:"), "%", &controls->m_mixModel,
		tr("Balance between wet and dry signals"));
	m_autoAttackKnob = makeKnob(false, tr("Auto Attack:"), "%", &controls->m_autoAttackModel,
		tr("Automatically control attack value depending on crest factor"));
	m_autoReleaseKnob = makeKnob(false, tr("Auto Release:"), "%", &controls->m_autoReleaseModel,
		tr("Automatically control release value depending on crest factor"));

	m_outFader = new EqFader(&controls->m_outGainModel,tr("Output gain"),
		this, &controls->m_outPeakL, &controls->m_outPeakR);
	m_outFader->setDisplayConversion(false);
	m_outFader->setHintText(tr("Gain"), "dBFS");
	m_outFader->setToolTip(tr("Output volume"));

	m_inFader = new EqFader(&controls->m_inGainModel,tr("Input gain"),
		this, &controls->m_inPeakL, &controls->m_inPeakR);
	m_inFader->setDisplayConversion(false);
	m_inFader->setHintText(tr("Gain"), "dBFS");
	m_inFader->setToolTip(tr("Input volume"));

	const auto makeButton = [this](const QString& name, const char* icon, const QString& tooltip,
		bool checkable = false) {
		auto* button = new PixmapButton(this, name);
		const auto stem = std::string(icon);
		button->setActiveGraphic(PLUGIN_NAME::getIconPixmap(stem + "_sel"));
		button->setInactiveGraphic(PLUGIN_NAME::getIconPixmap(stem + "_unsel"));
		button->setToolTip(tooltip);
		button->setCheckable(checkable);
		return button;
	};

	rmsButton = makeButton(tr("Root Mean Square"), "rms", tr("Use RMS of the input"));
	peakButton = makeButton(tr("Peak"), "peak", tr("Use absolute value of the input"));

	rmsPeakGroup = new AutomatableButtonGroup(this);
	rmsPeakGroup->addButton(rmsButton);
	rmsPeakGroup->addButton(peakButton);
	rmsPeakGroup->setModel(&controls->m_peakmodeModel);

	leftRightButton = makeButton(tr("Left/Right"), "leftright", tr("Compress left and right audio"));
	midSideButton = makeButton(tr("Mid/Side"), "midside", tr("Compress mid and side audio"));

	leftRightMidSideGroup = new AutomatableButtonGroup(this);
	leftRightMidSideGroup->addButton(leftRightButton);
	leftRightMidSideGroup->addButton(midSideButton);
	leftRightMidSideGroup->setModel(&controls->m_midsideModel);

	compressButton = makeButton(tr("Compressor"), "compressor", tr("Compress the audio"));
	limitButton = makeButton(tr("Limiter"), "limiter",
		tr("Set Ratio to infinity (is not guaranteed to limit audio volume)"));

	compressLimitGroup = new AutomatableButtonGroup(this);
	compressLimitGroup->addButton(compressButton);
	compressLimitGroup->addButton(limitButton);
	compressLimitGroup->setModel(&controls->m_limiterModel);

	unlinkedButton = makeButton(tr("Unlinked"), "unlinked", tr("Compress each channel separately"));
	maximumButton = makeButton(tr("Maximum"), "maximum", tr("Compress based on the loudest channel"));
	averageButton = makeButton(tr("Average"), "average", tr("Compress based on the averaged channel volume"));
	minimumButton = makeButton(tr("Minimum"), "minimum", tr("Compress based on the quietest channel"));
	blendButton = makeButton(tr("Blend"), "blend", tr("Blend between stereo linking modes"));

	stereoLinkGroup = new AutomatableButtonGroup(this);
	stereoLinkGroup->addButton(unlinkedButton);
	stereoLinkGroup->addButton(maximumButton);
	stereoLinkGroup->addButton(averageButton);
	stereoLinkGroup->addButton(minimumButton);
	stereoLinkGroup->addButton(blendButton);
	stereoLinkGroup->setModel(&controls->m_stereoLinkModel);

	autoMakeupButton = makeButton(tr("Auto Makeup Gain"), "autogain",
		tr("Automatically change makeup gain depending on threshold, knee, and ratio settings"), true);
	autoMakeupButton->setModel(&controls->m_autoMakeupModel);

	auditionButton = makeButton(tr("Soft Clip"), "audition", tr("Play the delta signal"), true);
	auditionButton->setModel(&controls->m_auditionModel);

	feedbackButton = makeButton(tr("Soft Clip"), "feedback",
		tr("Use the compressor's output as the sidechain input"), true);
	feedbackButton->setModel(&controls->m_feedbackModel);

	lookaheadButton = makeButton(tr("Lookahead Enabled"), "lookahead",
		tr("Enable Lookahead, which introduces 20 milliseconds of latency"), true);
	lookaheadButton->setModel(&controls->m_lookaheadModel);

	connect(getGUI()->mainWindow(), SIGNAL(periodicUpdate()), this, SLOT(updateDisplay()));

	connect(&m_controls->m_peakmodeModel, SIGNAL(dataChanged()), this, SLOT(peakmodeChanged()));
	connect(&m_controls->m_stereoLinkModel, SIGNAL(dataChanged()), this, SLOT(stereoLinkChanged()));
	connect(&m_controls->m_lookaheadModel, SIGNAL(dataChanged()), this, SLOT(lookaheadChanged()));
	connect(&m_controls->m_limiterModel, SIGNAL(dataChanged()), this, SLOT(limiterChanged()));

	m_timeElapsed.start();

	peakmodeChanged();
	stereoLinkChanged();
	lookaheadChanged();
	limiterChanged();
}


void CompressorControlDialog::makeLargeKnob(Knob * knob, QString hint, QString unit)
{
	knob->setHintText(hint, unit);
	knob->setFixedSize(56, 56);
	knob->setOuterRadius(23);
	knob->setInnerRadius(15);
	knob->setCenterPointX(28);
	knob->setCenterPointY(28);
}

void CompressorControlDialog::makeSmallKnob(Knob * knob, QString hint, QString unit)
{
	knob->setHintText(hint, unit);
	knob->setFixedSize(30, 30);
	knob->setOuterRadius(10);
	knob->setInnerRadius(4);
	knob->setCenterPointX(15);
	knob->setCenterPointY(15);
}


void CompressorControlDialog::peakmodeChanged()
{
	m_rmsKnob->setVisible(!m_controls->m_peakmodeModel.value());
	m_rmsEnabledLabel->setVisible(!m_controls->m_peakmodeModel.value());
}


void CompressorControlDialog::stereoLinkChanged()
{
	m_blendKnob->setVisible(m_controls->m_stereoLinkModel.value() == 4);
	m_blendEnabledLabel->setVisible(m_controls->m_stereoLinkModel.value() == 4);
}


void CompressorControlDialog::lookaheadChanged()
{
	m_lookaheadLengthKnob->setVisible(m_controls->m_lookaheadModel.value());
	m_lookaheadEnabledLabel->setVisible(m_controls->m_lookaheadModel.value());
	feedbackButton->setVisible(!m_controls->m_lookaheadModel.value());
}


void CompressorControlDialog::limiterChanged()
{
	m_ratioKnob->setVisible(!m_controls->m_limiterModel.value());
	m_ratioEnabledLabel->setVisible(!m_controls->m_limiterModel.value());
}


void CompressorControlDialog::updateDisplay()
{
	if (!isVisible())
	{
		m_timeElapsed.restart();
		return;
	}

	int elapsedMil = m_timeElapsed.elapsed();
	m_timeElapsed.restart();
	m_timeSinceLastUpdate += elapsedMil;
	m_compPixelMovement = int(m_timeSinceLastUpdate / COMP_MILLI_PER_PIXEL);
	m_timeSinceLastUpdate %= COMP_MILLI_PER_PIXEL;

	// Time Change / Daylight Savings Time protection
	if (!m_compPixelMovement || m_compPixelMovement <= 0)
	{
		return;
	}

	if (!m_controls->m_effect->isProcessingAudio())
	{
		m_controls->m_effect->m_displayPeak[0] = COMP_NOISE_FLOOR;
		m_controls->m_effect->m_displayPeak[1] = COMP_NOISE_FLOOR;
		m_controls->m_effect->m_displayGain[0] = 1;
		m_controls->m_effect->m_displayGain[1] = 1;
	}

	m_peakAvg = (m_controls->m_effect->m_displayPeak[0] + m_controls->m_effect->m_displayPeak[1]) * 0.5f;
	m_gainAvg = (m_controls->m_effect->m_displayGain[0] + m_controls->m_effect->m_displayGain[1]) * 0.5f;

	m_controls->m_effect->m_displayPeak[0] = m_controls->m_effect->m_yL[0];
	m_controls->m_effect->m_displayPeak[1] = m_controls->m_effect->m_yL[1];
	m_controls->m_effect->m_displayGain[0] = m_controls->m_effect->m_gainResult[0];
	m_controls->m_effect->m_displayGain[1] = m_controls->m_effect->m_gainResult[1];

	m_yPoint = dbfsToYPoint(ampToDbfs(m_peakAvg));
	m_yGainPoint = dbfsToYPoint(ampToDbfs(m_gainAvg));

	m_threshYPoint = dbfsToYPoint(m_controls->m_effect->m_thresholdVal);
	m_threshXPoint = m_kneeWindowSizeY - m_threshYPoint;

	drawVisPixmap();

	if (m_controls->m_effect->m_redrawKnee)
	{
		redrawKnee();
	}

	drawKneePixmap2();

	if (m_controls->m_effect->m_redrawThreshold)
	{
		drawMiscPixmap();
	}

	m_lastPoint = m_yPoint;
	m_lastGainPoint = m_yGainPoint;

	update();
}


void CompressorControlDialog::drawVisPixmap()
{
	m_p.begin(&m_visPixmap);

	// Move entire display to the left
	m_p.setCompositionMode(QPainter::CompositionMode_Source);
	m_p.drawPixmap(-m_compPixelMovement, 0, m_visPixmap);
	m_p.fillRect(m_windowSizeX-m_compPixelMovement, 0, m_windowSizeX, m_windowSizeY, QColor("transparent"));
	m_p.setCompositionMode(QPainter::CompositionMode_SourceOver);

	m_p.setRenderHint(QPainter::Antialiasing, true);

	// Draw translucent portion of input volume line
	m_p.setPen(QPen(m_inVolAreaColor, 1));
	for (int i = 0; i < m_compPixelMovement; ++i)
	{
		const int temp = std::lerp(
			m_lastPoint,
			m_yPoint,
			static_cast<float>(i) / static_cast<float>(m_compPixelMovement)
		);
		m_p.drawLine(m_windowSizeX-m_compPixelMovement+i, temp, m_windowSizeX-m_compPixelMovement+i, m_windowSizeY);
	}

	// Draw input volume line
	m_p.setPen(QPen(m_inVolColor, 1));
	m_p.drawLine(m_windowSizeX-m_compPixelMovement-1, m_lastPoint, m_windowSizeX, m_yPoint);

	// Draw translucent portion of output volume line
	m_p.setPen(QPen(m_outVolAreaColor, 1));
	for (int i = 0; i < m_compPixelMovement; ++i)
	{
		const int temp = std::lerp(
			m_lastPoint + m_lastGainPoint,
			m_yPoint + m_yGainPoint,
			static_cast<float>(i) / static_cast<float>(m_compPixelMovement)
		);
		m_p.drawLine(m_windowSizeX-m_compPixelMovement+i, temp, m_windowSizeX-m_compPixelMovement+i, m_windowSizeY);
	}

	// Draw output volume line
	m_p.setPen(QPen(m_outVolColor, 1));
	m_p.drawLine(m_windowSizeX-m_compPixelMovement-1, m_lastPoint+m_lastGainPoint, m_windowSizeX, m_yPoint+m_yGainPoint);

	// Draw gain reduction line
	m_p.setPen(QPen(m_gainReductionColor, 2));
	m_p.drawLine(m_windowSizeX-m_compPixelMovement-1, m_lastGainPoint, m_windowSizeX, m_yGainPoint);

	m_p.end();
}


void CompressorControlDialog::redrawKnee()
{
	m_controls->m_effect->m_redrawKnee = false;

	// Start drawing knee visualizer
	m_p.begin(&m_kneePixmap);

	m_p.setRenderHint(QPainter::Antialiasing, false);

	// Clear display
	m_p.setCompositionMode(QPainter::CompositionMode_Source);
	m_p.fillRect(0, 0, m_windowSizeX, m_kneeWindowSizeY, QColor("transparent"));
	m_p.setCompositionMode(QPainter::CompositionMode_SourceOver);

	m_p.setRenderHint(QPainter::Antialiasing, true);

	m_p.setPen(QPen(m_kneeColor, 3));

	// Limiter = infinite ratio
	float actualRatio = m_controls->m_limiterModel.value() ? 0 : m_controls->m_effect->m_ratioVal;

	// Calculate endpoints for the two straight lines
	const float thresholdVal = m_controls->m_effect->m_thresholdVal;
	const float kneeVal = m_controls->m_effect->m_kneeVal;
	float kneePoint1 = thresholdVal - kneeVal;
	float kneePoint2X = thresholdVal + kneeVal;
	float kneePoint2Y = thresholdVal + kneeVal * actualRatio;
	float ratioPoint = thresholdVal + (-thresholdVal * actualRatio);

	// Draw two straight lines
	m_p.drawLine(0, m_kneeWindowSizeY, dbfsToXPoint(kneePoint1), dbfsToYPoint(kneePoint1));
	if (dbfsToXPoint(kneePoint2X) < m_kneeWindowSizeY)
	{
		m_p.drawLine(dbfsToXPoint(kneePoint2X), dbfsToYPoint(kneePoint2Y), m_kneeWindowSizeY, dbfsToYPoint(ratioPoint));
	}

	// Draw knee section
	if (kneeVal)
	{
		m_p.setPen(QPen(m_kneeColor2, 3));

		auto prevPoint = std::array{kneePoint1, kneePoint1};
		auto newPoint = std::array{0.f, 0.f};

		// Draw knee curve using many straight lines.
		for (int i = 0; i < COMP_KNEE_LINES; ++i)
		{
			newPoint[0] = std::lerp(kneePoint1, kneePoint2X, (i + 1) / static_cast<float>(COMP_KNEE_LINES));

			const float temp = newPoint[0] - thresholdVal + kneeVal;
			newPoint[1] = (newPoint[0] + (actualRatio - 1) * temp * temp / (4 * kneeVal));

			m_p.drawLine(dbfsToXPoint(prevPoint[0]), dbfsToYPoint(prevPoint[1]), dbfsToXPoint(newPoint[0]), dbfsToYPoint(newPoint[1]));

			prevPoint[0] = newPoint[0];
			prevPoint[1] = newPoint[1];
		}
	}

	m_p.setRenderHint(QPainter::Antialiasing, false);

	// Erase right portion
	m_p.setCompositionMode(QPainter::CompositionMode_Source);
	m_p.fillRect(m_kneeWindowSizeX + 1, 0, m_windowSizeX, m_kneeWindowSizeY, QColor("transparent"));
	m_p.setCompositionMode(QPainter::CompositionMode_SourceOver);

	m_p.end();

	m_p.begin(&m_kneePixmap2);

	m_p.setCompositionMode(QPainter::CompositionMode_Source);
	m_p.fillRect(0, 0, m_windowSizeX, m_kneeWindowSizeY, QColor("transparent"));
	m_p.setCompositionMode(QPainter::CompositionMode_SourceOver);

	m_p.end();

	m_lastKneePoint = 0;
}


void CompressorControlDialog::drawKneePixmap2()
{
	m_p.begin(&m_kneePixmap2);

	m_p.setRenderHint(QPainter::Antialiasing, false);

	int kneePoint = dbfsToXPoint(ampToDbfs(m_peakAvg));
	if (kneePoint > m_lastKneePoint)
	{
		QRectF knee2Rect = QRect(m_lastKneePoint, 0, kneePoint - m_lastKneePoint, m_kneeWindowSizeY);
		m_p.drawPixmap(knee2Rect, m_kneePixmap, knee2Rect);
	}
	else
	{
		m_p.setCompositionMode(QPainter::CompositionMode_Source);
		m_p.fillRect(kneePoint, 0, m_lastKneePoint, m_kneeWindowSizeY, QColor("transparent"));
		m_p.setCompositionMode(QPainter::CompositionMode_SourceOver);
	}
	m_lastKneePoint = kneePoint;

	m_p.end();
}


void CompressorControlDialog::drawMiscPixmap()
{
	m_p.begin(&m_miscPixmap);

	m_p.setCompositionMode(QPainter::CompositionMode_Source);
	m_p.fillRect(0, 0, m_windowSizeX, m_windowSizeY, QColor("transparent"));
	m_p.setCompositionMode(QPainter::CompositionMode_SourceOver);

	m_p.setRenderHint(QPainter::Antialiasing, true);

	// Draw threshold lines
	m_p.setPen(QPen(m_threshColor, 2, Qt::DotLine));
	m_p.drawLine(0, m_threshYPoint, m_windowSizeX, m_threshYPoint);
	m_p.drawLine(m_threshXPoint, 0, m_threshXPoint, m_kneeWindowSizeY);

	m_p.end();

	m_controls->m_effect->m_redrawThreshold = false;
}



void CompressorControlDialog::paintEvent(QPaintEvent *event)
{
	if (!isVisible())
	{
		return;
	}

	m_p.begin(this);

	m_p.setCompositionMode(QPainter::CompositionMode_Source);
	m_p.fillRect(0, 0, m_windowSizeX, m_windowSizeY, m_backgroundColor);
	m_p.setCompositionMode(QPainter::CompositionMode_SourceOver);

	m_p.drawPixmap(0, 0, m_graphPixmap);
	m_p.drawPixmap(0, 0, m_visPixmap);
	m_p.setOpacity(0.25);
	m_p.drawPixmap(0, 0, m_kneePixmap);
	m_p.setOpacity(1);
	if (m_controls->m_effect->isProcessingAudio())
	{
		m_p.drawPixmap(0, 0, m_kneePixmap2);
	}
	m_p.drawPixmap(0, 0, m_miscPixmap);

	m_p.end();
}


inline int CompressorControlDialog::dbfsToYPoint(float inDbfs)
{
	return (-((inDbfs + m_dbRange) / m_dbRange) + 1) * m_windowSizeY;
}

inline int CompressorControlDialog::dbfsToXPoint(float inDbfs)
{
	return m_kneeWindowSizeY - dbfsToYPoint(inDbfs);
}


void CompressorControlDialog::resizeEvent(QResizeEvent *event)
{
	resetCompressorView();
}


void CompressorControlDialog::wheelEvent(QWheelEvent * event)
{
	const float temp = m_dbRange;
	const float dbRangeNew = m_dbRange - copysignf(COMP_GRID_SPACING, event->angleDelta().y());
	m_dbRange = round(qBound(COMP_GRID_SPACING, dbRangeNew, COMP_GRID_MAX) / COMP_GRID_SPACING) * COMP_GRID_SPACING;

	// Only reset view if the scolling had an effect
	if (m_dbRange != temp)
	{
		drawGraph();
		m_controls->m_effect->m_redrawKnee = true;
		m_controls->m_effect->m_redrawThreshold = true;
	}
}


void CompressorControlDialog::drawGraph()
{
	m_p.begin(&m_graphPixmap);

	m_p.setRenderHint(QPainter::Antialiasing, false);

	m_p.setCompositionMode(QPainter::CompositionMode_Source);
	m_p.fillRect(0, 0, m_windowSizeX, m_windowSizeY, QColor("transparent"));
	m_p.setCompositionMode(QPainter::CompositionMode_SourceOver);

	m_p.setPen(QPen(m_textColor, 1));

	// Arbitrary formula for increasing font size when window size increases
	m_p.setFont(QFont("Arial", qMax(int(m_windowSizeY / 1080.f * 24), 12)));

	// Redraw graph
	m_p.setPen(QPen(m_graphColor, 1));
	for (int i = 0; i < m_dbRange / COMP_GRID_SPACING + 1; ++i)
	{
		m_p.drawLine(0, dbfsToYPoint(-COMP_GRID_SPACING * i), m_windowSizeX, dbfsToYPoint(-COMP_GRID_SPACING * i));
		m_p.drawLine(dbfsToXPoint(-COMP_GRID_SPACING * i), 0, dbfsToXPoint(-COMP_GRID_SPACING * i), m_kneeWindowSizeY);
		m_p.drawText(QRectF(m_windowSizeX - 50, dbfsToYPoint(-COMP_GRID_SPACING * i), 50, 50), Qt::AlignRight | Qt::AlignTop, QString::number(i * -COMP_GRID_SPACING));
	}

	m_p.end();
}


void CompressorControlDialog::mouseDoubleClickEvent(QMouseEvent* event)
{
	setGuiVisibility(!m_guiVisibility);
}


void CompressorControlDialog::setGuiVisibility(bool isVisible)
{
	if (!isVisible)
	{
		m_rmsKnob->setVisible(isVisible);
		m_rmsEnabledLabel->setVisible(isVisible);

		m_lookaheadLengthKnob->setVisible(isVisible);
		m_lookaheadEnabledLabel->setVisible(isVisible);

		m_blendKnob->setVisible(isVisible);
		m_blendEnabledLabel->setVisible(isVisible);

		m_ratioKnob->setVisible(isVisible);
		m_ratioEnabledLabel->setVisible(isVisible);
	}
	else
	{
		m_rmsKnob->setVisible(!m_controls->m_peakmodeModel.value());
		m_rmsEnabledLabel->setVisible(!m_controls->m_peakmodeModel.value());

		m_blendKnob->setVisible(m_controls->m_stereoLinkModel.value() == 4);
		m_blendEnabledLabel->setVisible(m_controls->m_stereoLinkModel.value() == 4);

		m_lookaheadLengthKnob->setVisible(m_controls->m_lookaheadModel.value());
		m_lookaheadEnabledLabel->setVisible(m_controls->m_lookaheadModel.value());

		m_ratioKnob->setVisible(!m_controls->m_limiterModel.value());
		m_ratioEnabledLabel->setVisible(!m_controls->m_limiterModel.value());
	}
	m_controlsBoxLabel->setVisible(isVisible);
	m_thresholdKnob->setVisible(isVisible);
	m_attackKnob->setVisible(isVisible);
	m_releaseKnob->setVisible(isVisible);
	m_kneeKnob->setVisible(isVisible);
	m_rangeKnob->setVisible(isVisible);
	m_holdKnob->setVisible(isVisible);
	m_inBalanceKnob->setVisible(isVisible);
	m_outBalanceKnob->setVisible(isVisible);
	m_stereoBalanceKnob->setVisible(isVisible);
	m_tiltKnob->setVisible(isVisible);
	m_tiltFreqKnob->setVisible(isVisible);
	m_mixKnob->setVisible(isVisible);
	m_autoAttackKnob->setVisible(isVisible);
	m_autoReleaseKnob->setVisible(isVisible);
	m_outFader->setVisible(isVisible);
	m_inFader->setVisible(isVisible);
	rmsButton->setVisible(isVisible);
	peakButton->setVisible(isVisible);
	rmsPeakGroup->setVisible(isVisible);
	leftRightButton->setVisible(isVisible);
	midSideButton->setVisible(isVisible);
	compressButton->setVisible(isVisible);
	limitButton->setVisible(isVisible);
	unlinkedButton->setVisible(isVisible);
	maximumButton->setVisible(isVisible);
	averageButton->setVisible(isVisible);
	minimumButton->setVisible(isVisible);
	blendButton->setVisible(isVisible);
	autoMakeupButton->setVisible(isVisible);
	auditionButton->setVisible(isVisible);
	feedbackButton->setVisible(isVisible);
	lookaheadButton->setVisible(isVisible);
	m_guiVisibility = isVisible;
}


void CompressorControlDialog::resetCompressorView()
{
	m_windowSizeX = size().width();
	m_windowSizeY = size().height();
	m_kneeWindowSizeX = m_windowSizeY;
	m_kneeWindowSizeY = m_windowSizeY;
	m_controlsBoxX = (m_windowSizeX - COMP_BOX_X) * 0.5;
	m_controlsBoxY = m_windowSizeY - 40 - COMP_BOX_Y;

	m_controls->m_effect->m_redrawKnee = true;
	m_controls->m_effect->m_redrawThreshold = true;
	m_lastKneePoint = 0;

	drawGraph();

	m_p.begin(&m_visPixmap);

	m_p.setCompositionMode(QPainter::CompositionMode_Source);
	m_p.fillRect(0, 0, m_windowSizeX, m_windowSizeY, QColor("transparent"));
	m_p.setCompositionMode(QPainter::CompositionMode_SourceOver);

	// Draw line at right side, so the sudden
	// content that the visualizer will display
	// later on won't look too ugly
	m_p.setPen(QPen(m_resetColor, 3));
	m_p.drawLine(m_windowSizeX, 0, m_windowSizeX, m_windowSizeY);

	m_p.end();

	m_controlsBoxLabel->move(m_controlsBoxX, m_controlsBoxY);
	m_rmsEnabledLabel->move(m_controlsBoxX + 429, m_controlsBoxY + 209);
	m_blendEnabledLabel->move(m_controlsBoxX + 587, m_controlsBoxY + 197);
	m_lookaheadEnabledLabel->move(m_controlsBoxX + 221, m_controlsBoxY + 135);
	m_ratioEnabledLabel->move(m_controlsBoxX + 267, m_controlsBoxY + 21);

	m_thresholdKnob->move(m_controlsBoxX + 137, m_controlsBoxY + 21);
	m_ratioKnob->move(m_controlsBoxX + 267, m_controlsBoxY + 21);
	m_attackKnob->move(m_controlsBoxX + 397, m_controlsBoxY + 21);
	m_releaseKnob->move(m_controlsBoxX + 527, m_controlsBoxY + 21);
	m_kneeKnob->move(m_controlsBoxX + 97, m_controlsBoxY + 135);
	m_rangeKnob->move(m_controlsBoxX + 159, m_controlsBoxY + 135);
	m_lookaheadLengthKnob->move(m_controlsBoxX + 221, m_controlsBoxY + 135);
	m_holdKnob->move(m_controlsBoxX + 283, m_controlsBoxY + 135);

	m_rmsKnob->move(m_controlsBoxX + 429, m_controlsBoxY + 209);
	m_inBalanceKnob->move(m_controlsBoxX + 27, m_controlsBoxY + 219);
	m_outBalanceKnob->move(m_controlsBoxX + 662, m_controlsBoxY + 219);
	m_stereoBalanceKnob->move(m_controlsBoxX + 522, m_controlsBoxY + 137);
	m_blendKnob->move(m_controlsBoxX + 587, m_controlsBoxY + 197);
	m_tiltKnob->move(m_controlsBoxX + 364, m_controlsBoxY + 138);
	m_tiltFreqKnob->move(m_controlsBoxX + 415, m_controlsBoxY + 138);
	m_mixKnob->move(m_controlsBoxX + 27, m_controlsBoxY + 13);

	m_outFader->move(m_controlsBoxX + 666, m_controlsBoxY + 91);
	m_inFader->move(m_controlsBoxX + 31, m_controlsBoxY + 91);

	rmsButton->move(m_controlsBoxX + 337, m_controlsBoxY + 231);
	peakButton->move(m_controlsBoxX + 337, m_controlsBoxY + 248);

	leftRightButton->move(m_controlsBoxX + 220, m_controlsBoxY + 231);
	midSideButton->move(m_controlsBoxX + 220, m_controlsBoxY + 248);

	compressButton->move(m_controlsBoxX + 98, m_controlsBoxY + 231);
	limitButton->move(m_controlsBoxX + 98, m_controlsBoxY + 248);

	unlinkedButton->move(m_controlsBoxX + 495, m_controlsBoxY + 180);
	maximumButton->move(m_controlsBoxX + 495, m_controlsBoxY + 197);
	averageButton->move(m_controlsBoxX + 495, m_controlsBoxY + 214);
	minimumButton->move(m_controlsBoxX + 495, m_controlsBoxY + 231);
	blendButton->move(m_controlsBoxX + 495, m_controlsBoxY + 248);

	autoMakeupButton->move(m_controlsBoxX + 220, m_controlsBoxY + 206);
	auditionButton->move(m_controlsBoxX + 658, m_controlsBoxY + 14);
	feedbackButton->move(m_controlsBoxX + 98, m_controlsBoxY + 206);
	m_autoAttackKnob->move(m_controlsBoxX + 460, m_controlsBoxY + 38);
	m_autoReleaseKnob->move(m_controlsBoxX + 590, m_controlsBoxY + 38);
	lookaheadButton->move(m_controlsBoxX + 202, m_controlsBoxY + 171);
}


} // namespace mxm::gui
