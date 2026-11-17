/*
 * SampleTrackView.cpp
 *
 * Copyright (c) 2005-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "SampleTrackView.h"

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QHBoxLayout>
#include <QMenu>
#include <QProgressBar>
#include <QSignalBlocker>
#include <QSpacerItem>
#include <QToolButton>
#include <QVBoxLayout>

#include "ConfigManager.h"
#include "DeprecationHelper.h"
#include "MainWindow.h"
#include "embed.h"
#include "Engine.h"
#include "FadeButton.h"
#include "Mixer.h"
#include "MixerView.h"
#include "GuiApplication.h"
#include "Knob.h"
#include "SampleClip.h"
#include "Song.h"
#include "SampleTrackWindow.h"
#include "SongEditor.h"
#include "StringPairDrag.h"
#include "TrackContainerView.h"
#include "TrackLabelButton.h"


namespace mxm::gui
{


SampleTrackView::SampleTrackView( SampleTrack * _t, TrackContainerView* tcv ) :
	TrackView( _t, tcv )
{
	setFixedHeight( 32 );

	m_tlb = new TrackLabelButton(this, getTrackSettingsWidget());
	m_tlb->setCheckable(true);
	connect(m_tlb, SIGNAL(clicked(bool)),
			this, SLOT(showEffects()));
	m_tlb->setIcon(embed::getIconPixmap("sample_track"));
	m_tlb->show();

	m_volumeKnob = new VolumeKnob(KnobType::Small17, tr("VOL"), getTrackSettingsWidget(), Knob::LabelRendering::LegacyFixedFontSize, tr("Track volume"));
	m_volumeKnob->setModel( &_t->m_volumeModel );
	m_volumeKnob->setHintText(tr("Volume:"), "%");
	m_volumeKnob->show();

	m_panningKnob = new Knob(KnobType::Small17, tr("PAN"), getTrackSettingsWidget(), Knob::LabelRendering::LegacyFixedFontSize, tr("Panning"));
	m_panningKnob->setModel( &_t->m_panningModel );
	m_panningKnob->setHintText( tr( "Panning:" ), "%" );
	m_panningKnob->show();

	m_activityIndicator = new FadeButton(getTrackSettingsWidget());
	m_activityIndicator->setFixedSize(8, 28);
	m_activityIndicator->show();

	auto recordControls = new QWidget(getTrackSettingsWidget());
	auto recordControlsLayout = new QVBoxLayout(recordControls);
	recordControlsLayout->setContentsMargins(0, 0, 0, 0);
	recordControlsLayout->setSpacing(1);

	m_recordArmButton = new QToolButton(recordControls);
	m_recordArmButton->setObjectName("recordArmButton");
	m_recordArmButton->setText(tr("ARM"));
	m_recordArmButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
	m_recordArmButton->setCheckable(true);
	m_recordArmButton->setFixedSize(30, 18);
	m_recordArmButton->setToolTip(tr("Arm this sample track for recording"));
	m_recordArmButton->setStyleSheet(
		"QToolButton { border: 1px solid palette(mid); border-radius: 2px; padding: 0; font-size: 8px; }"
		"QToolButton:checked { background-color: #d32f2f; color: white; }");
	m_recordArmButton->setChecked(_t->isRecordArmed());
	connect(m_recordArmButton, &QToolButton::toggled, this, [this](bool armed) {
		model()->setRecordArmed(armed);
	});
	recordControlsLayout->addWidget(m_recordArmButton);

	m_inputLevelMeter = new QProgressBar(recordControls);
	m_inputLevelMeter->setObjectName("inputLevelMeter");
	m_inputLevelMeter->setRange(0, 100);
	m_inputLevelMeter->setValue(0);
	m_inputLevelMeter->setTextVisible(false);
	m_inputLevelMeter->setFixedSize(30, 7);
	m_inputLevelMeter->setToolTip(tr("Audio input level"));
	m_inputLevelMeter->setAccessibleName(tr("Audio input level"));
	m_inputLevelMeter->setStyleSheet(
		"QProgressBar { border: 1px solid palette(mid); border-radius: 2px; background: palette(base); }"
		"QProgressBar::chunk { background-color: #55b86a; }");
	recordControlsLayout->addWidget(m_inputLevelMeter);

	auto masterLayout = new QVBoxLayout(getTrackSettingsWidget());
	masterLayout->setContentsMargins(0, 1, 0, 0);
	auto layout = new QHBoxLayout();
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(m_tlb);
	layout->addWidget(recordControls, 0, Qt::AlignVCenter);
	layout->addWidget(m_activityIndicator);
	layout->addWidget(m_volumeKnob);
	layout->addWidget(m_panningKnob);
	masterLayout->addLayout(layout);
	masterLayout->addSpacerItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));

	connect(_t, SIGNAL(playingChanged()), this, SLOT(updateIndicator()));
	connect(getGUI()->mainWindow(), &MainWindow::periodicUpdate, this, &SampleTrackView::corruptStateUpdate);
	connect(getGUI()->mainWindow(), &MainWindow::periodicUpdate, this, &SampleTrackView::updateRecordingControls);

	setModel( _t );

	m_window = new SampleTrackWindow(this);
	m_window->toggleVisibility(false);
	updateRecordingControls();
}




void SampleTrackView::updateIndicator()
{
	if (model()->isPlaying()) { m_activityIndicator->activateOnce(); }
	else { m_activityIndicator->noteEnd(); }
}




SampleTrackView::~SampleTrackView()
{
	if(m_window != nullptr)
	{
		m_window->setSampleTrackView(nullptr);
		m_window->parentWidget()->hide();
	}
	m_window = nullptr;
}



//FIXME: This is identical to InstrumentTrackView::createMixerMenu
QMenu * SampleTrackView::createMixerMenu(QString title, QString newMixerLabel)
{
	int channelIndex = model()->mixerChannelModel()->value();

	MixerChannel *mixerChannel = Engine::mixer()->mixerChannel(channelIndex);

	// If title allows interpolation, pass channel index and name
	if (title.contains("%2"))
	{
		title = title.arg(channelIndex).arg(mixerChannel->m_name);
	}

	auto mixerMenu = new QMenu(title);

	mixerMenu->addAction(newMixerLabel, this, SLOT(createMixerLine()));
	mixerMenu->addSeparator();

	for (int i = 0; i < Engine::mixer()->numChannels(); ++i)
	{
		MixerChannel * currentChannel = Engine::mixer()->mixerChannel(i);

		if (currentChannel != mixerChannel)
		{
			const auto index = currentChannel->index();
			QString label = tr("%1: %2").arg(index).arg(currentChannel->m_name);
			mixerMenu->addAction(label, [this, index](){
				assignMixerLine(index);
			});
		}
	}

	return mixerMenu;
}




void SampleTrackView::showEffects()
{
	m_window->toggleVisibility(m_window->parentWidget()->isHidden());
}



void SampleTrackView::modelChanged()
{
	auto st = castModel<SampleTrack>();
	m_volumeKnob->setModel(&st->m_volumeModel);
	updateRecordingControls();

	TrackView::modelChanged();
}




void SampleTrackView::dragEnterEvent(QDragEnterEvent *dee)
{
	StringPairDrag::processDragEnterEvent(dee, QString("samplefile"));
}




void SampleTrackView::dropEvent(QDropEvent *de)
{
	QString type  = StringPairDrag::decodeKey(de);
	QString value = StringPairDrag::decodeValue(de);

	if (type == "samplefile")
	{
		int trackHeadWidth = ConfigManager::inst()->value("ui", "compacttrackbuttons").toInt()==1
				? DEFAULT_SETTINGS_WIDGET_WIDTH_COMPACT + TRACK_OP_WIDTH_COMPACT
				: DEFAULT_SETTINGS_WIDGET_WIDTH + TRACK_OP_WIDTH;

		const int deX = position(de).x();
		int xPos = deX < trackHeadWidth
				? trackHeadWidth
				: deX;

		const float snapSize = getGUI()->songEditor()->m_editor->getSnapSize();
		TimePos clipPos = trackContainerView()->fixedClips()
				? TimePos(0)
				: TimePos(((xPos - trackHeadWidth) / trackContainerView()->pixelsPerBar()
							* TimePos::ticksPerBar()) + trackContainerView()->currentPosition()
						).quantize(snapSize, true);

		auto sClip = static_cast<SampleClip*>(getTrack()->createClip(clipPos));
		if (sClip) { sClip->setSampleFile(value); }
	}
}




void SampleTrackView::createMixerLine()
{
	int channelIndex = getGUI()->mixerView()->addNewChannel();
	auto channel = Engine::mixer()->mixerChannel(channelIndex);

	channel->m_name = getTrack()->name();
	channel->setColor(getTrack()->color());

	assignMixerLine(channelIndex);
}




void SampleTrackView::assignMixerLine(int channelIndex)
{
	model()->mixerChannelModel()->setValue(channelIndex);

	getGUI()->mixerView()->setCurrentMixerChannel(channelIndex);
}

void SampleTrackView::corruptStateUpdate()
{
	if (model()->audioBusHandle()->isCorrupted())
	{
		m_activityIndicator->setState(FadeButton::State::Corrupted);
		m_activityIndicator->setToolTip(tr("Corrupted audio detected: muting affected channels"));
	}
	else
	{
		m_activityIndicator->setState(FadeButton::State::Normal);
		m_activityIndicator->setToolTip(QString{});
	}
}

void SampleTrackView::updateRecordingControls()
{
	auto track = model();
	if (m_recordArmButton->isChecked() != track->isRecordArmed())
	{
		const QSignalBlocker blocker(m_recordArmButton);
		m_recordArmButton->setChecked(track->isRecordArmed());
	}
	const bool recording = Engine::getSong()->isRecording() && track->isRecordArmed();
	m_recordArmButton->setText(recording ? tr("REC") : tr("ARM"));
	m_recordArmButton->setToolTip(recording ? tr("Recording this sample track") :
		tr("Arm this sample track for recording"));

	const auto inputLevel = std::clamp(track->takeInputLevel(), 0.f, 1.f);
	m_inputLevelMeter->setValue(static_cast<int>(std::round(inputLevel * 100.f)));
}


} // namespace mxm::gui
