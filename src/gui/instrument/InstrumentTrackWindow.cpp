/*
 * InstrumentTrackWindow.cpp - implementation of InstrumentTrackWindow class
 *
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "InstrumentTrackWindow.h"

#include <QDir>
#include <QDropEvent>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>

#include "AutomatableButton.h"
#include "ComboBox.h"
#include "ConfigManager.h"
#include "DataFile.h"
#include "EffectRackView.h"
#include "embed.h"
#include "Engine.h"
#include "FileBrowser.h"
#include "FileDialog.h"
#include "GroupBox.h"
#include "MixerChannelLcdSpinBox.h"
#include "GuiApplication.h"
#include "Instrument.h"
#include "InstrumentFunctions.h"
#include "InstrumentFunctionViews.h"
#include "InstrumentMidiIOView.h"
#include "InstrumentTuningView.h"
#include "InstrumentSoundShapingView.h"
#include "InstrumentTrack.h"
#include "InstrumentTrackView.h"
#include "Knob.h"
#include "LcdSpinBox.h"
#include "LedCheckBox.h"
#include "LeftRightNav.h"
#include "MainWindow.h"
#include "PianoView.h"
#include "PianoRoll.h"
#include "PluginFactory.h"
#include "PluginView.h"
#include "Song.h"
#include "StringPairDrag.h"
#include "SubWindow.h"
#include "TabWidget.h"
#include "TrackContainerView.h"
#include "TrackLabelButton.h"


namespace mxm::gui
{


const int INSTRUMENT_WIDTH	= 254;
const int INSTRUMENT_HEIGHT	= INSTRUMENT_WIDTH;
const int PIANO_HEIGHT		= 80;
const int PIANO_RECORD_CONTROLS_HEIGHT = 22;


InstrumentTrackWindow::InstrumentTrackWindow( InstrumentTrackView * _itv ) :
	QWidget(),
	ModelView( nullptr, this ),
	m_track( _itv->model() ),
	m_itv( _itv ),
	m_instrumentView( nullptr )
{
	setAcceptDrops( true );

	// init own layout + widgets
	setFocusPolicy( Qt::StrongFocus );
	auto vlayout = new QVBoxLayout(this);
	vlayout->setContentsMargins(0, 0, 0, 0);
	vlayout->setSpacing( 0 );

	auto generalSettingsWidget = new QWidget(this);
	auto generalSettingsLayout = new QVBoxLayout(generalSettingsWidget);

	auto nameAndChangeTrackWidget = new QWidget(generalSettingsWidget);
	auto nameAndChangeTrackLayout = new QHBoxLayout(nameAndChangeTrackWidget);
	nameAndChangeTrackLayout->setContentsMargins( 0, 0, 0, 0 );
	nameAndChangeTrackLayout->setSpacing( 2 );

	// setup line edit for changing instrument track name
	m_nameLineEdit = new QLineEdit;
	connect( m_nameLineEdit, SIGNAL( textChanged( const QString& ) ),
				this, SLOT( textChanged( const QString& ) ) );

	m_nameLineEdit->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed));
	nameAndChangeTrackLayout->addWidget(m_nameLineEdit, 1);


	// set up left/right arrows for changing instrument
	m_leftRightNav = new LeftRightNav(this);
	connect( m_leftRightNav, SIGNAL(onNavLeft()), this,
						SLOT(viewPrevInstrument()));
	connect( m_leftRightNav, SIGNAL(onNavRight()), this,
						SLOT(viewNextInstrument()));
	// m_leftRightNav->setShortcuts();
	nameAndChangeTrackLayout->addWidget(m_leftRightNav);

	nameAndChangeTrackWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	generalSettingsLayout->addWidget( nameAndChangeTrackWidget );

	auto basicControlsLayout = new QGridLayout;
	basicControlsLayout->setHorizontalSpacing(3);
	basicControlsLayout->setVerticalSpacing(0);
	basicControlsLayout->setContentsMargins(0, 0, 0, 0);

	QString labelStyleSheet = "font-size: 10px;";
	Qt::Alignment labelAlignment = Qt::AlignHCenter | Qt::AlignTop;
	Qt::Alignment widgetAlignment = Qt::AlignHCenter | Qt::AlignCenter;
	
	auto soloMuteLayout = new QVBoxLayout();
	soloMuteLayout->setContentsMargins(0, 0, 2, 0);
	soloMuteLayout->setSpacing(2);

	m_muteBtn = new AutomatableButton(this, tr("Mute"));
	m_muteBtn->setModel(&m_track->m_mutedModel);
	m_muteBtn->setCheckable(true);
	m_muteBtn->setObjectName("btn-mute");
	m_muteBtn->setToolTip(tr("Mute this instrument"));
	soloMuteLayout->addWidget(m_muteBtn, 0, widgetAlignment);

	m_soloBtn = new AutomatableButton(this, tr("Solo"));
	m_soloBtn->setModel(&m_track->m_soloModel);
	m_soloBtn->setCheckable(true);
	m_soloBtn->setObjectName("btn-solo");
	m_soloBtn->setToolTip(tr("Solo this instrument"));
	soloMuteLayout->addWidget(m_soloBtn, 0, widgetAlignment);

	basicControlsLayout->addLayout(soloMuteLayout, 0, 0, 2, 1, widgetAlignment);

	// set up volume knob
	m_volumeKnob = new VolumeKnob(KnobType::Bright26, nullptr, tr("Volume"));
	m_volumeKnob->setHintText( tr( "Volume:" ), "%" );

	basicControlsLayout->addWidget(m_volumeKnob, 0, 1);
	basicControlsLayout->setAlignment( m_volumeKnob, widgetAlignment );

	auto label = new QLabel(tr("VOL"), this);
	label->setStyleSheet(labelStyleSheet);
	basicControlsLayout->addWidget(label, 1, 1);
	basicControlsLayout->setAlignment(label, labelAlignment);

	// set up panning knob
	m_panningKnob = new Knob( KnobType::Bright26, nullptr, tr( "Panning" ) );
	m_panningKnob->setHintText( tr( "Panning:" ), "" );

	basicControlsLayout->addWidget(m_panningKnob, 0, 2);
	basicControlsLayout->setAlignment( m_panningKnob, widgetAlignment );

	label = new QLabel( tr( "PAN" ), this );
	label->setStyleSheet( labelStyleSheet );
	basicControlsLayout->addWidget(label, 1, 2);
	basicControlsLayout->setAlignment( label, labelAlignment );

	basicControlsLayout->setColumnStretch(3, 1);

	// Output channel layout (mono through surround). The track bus converts
	// into the destination mixer channel layout by speaker role.
	m_channelLayoutComboBox = new ComboBox(this, tr("Output channel layout"));
	m_channelLayoutComboBox->setFixedWidth(82);
	m_channelLayoutComboBox->setToolTip(tr("Output channel layout of this instrument track"));
	basicControlsLayout->addWidget(m_channelLayoutComboBox, 0, 3);
	basicControlsLayout->setAlignment(m_channelLayoutComboBox, widgetAlignment);

	label = new QLabel(tr("OUT"), this);
	label->setStyleSheet(labelStyleSheet);
	basicControlsLayout->addWidget(label, 1, 3);
	basicControlsLayout->setAlignment(label, labelAlignment);

	// set up pitch knob
	m_pitchKnob = new Knob( KnobType::Bright26, nullptr, tr( "Pitch" ) );
	m_pitchKnob->setHintText( tr( "Pitch:" ), " " + tr( "cents" ) );

	basicControlsLayout->addWidget(m_pitchKnob, 0, 4);
	basicControlsLayout->setAlignment( m_pitchKnob, widgetAlignment );

	m_pitchLabel = new QLabel( tr( "PITCH" ), this );
	m_pitchLabel->setStyleSheet( labelStyleSheet );
	basicControlsLayout->addWidget(m_pitchLabel, 1, 4);
	basicControlsLayout->setAlignment( m_pitchLabel, labelAlignment );

	// set up pitch range knob
	m_pitchRangeSpinBox= new LcdSpinBox( 2, nullptr, tr( "Pitch range (semitones)" ) );

	basicControlsLayout->addWidget(m_pitchRangeSpinBox, 0, 5);
	basicControlsLayout->setAlignment( m_pitchRangeSpinBox, widgetAlignment );

	m_pitchRangeLabel = new QLabel( tr( "RANGE" ), this );
	m_pitchRangeLabel->setStyleSheet( labelStyleSheet );
	basicControlsLayout->addWidget(m_pitchRangeLabel, 1, 5);
	basicControlsLayout->setAlignment( m_pitchRangeLabel, labelAlignment );

	basicControlsLayout->setColumnStretch(6, 1);

	// setup spinbox for selecting Mixer-channel
	m_mixerChannelNumber = new MixerChannelLcdSpinBox(2, nullptr, tr("Mixer channel"), m_itv);

	basicControlsLayout->addWidget(m_mixerChannelNumber, 0, 7);
	basicControlsLayout->setAlignment( m_mixerChannelNumber, widgetAlignment );

	label = new QLabel(tr("CHAN"), this);
	label->setStyleSheet( labelStyleSheet );
	basicControlsLayout->addWidget(label, 1, 7);
	basicControlsLayout->setAlignment( label, labelAlignment );

	auto saveSettingsBtn = new QPushButton(embed::getIconPixmap("project_save"), QString());
	saveSettingsBtn->setMinimumSize( 32, 32 );

	connect( saveSettingsBtn, SIGNAL(clicked()), this, SLOT(saveSettingsBtnClicked()));

	saveSettingsBtn->setToolTip(tr("Save current instrument track settings in a preset file"));

	basicControlsLayout->addWidget(saveSettingsBtn, 0, 8);

	label = new QLabel( tr( "SAVE" ), this );
	label->setStyleSheet( labelStyleSheet );
	basicControlsLayout->addWidget(label, 1, 8);
	basicControlsLayout->setAlignment( label, labelAlignment );

	generalSettingsLayout->addLayout( basicControlsLayout );


	m_tabWidget = new TabWidget( "", this, true, true );
	// "-1" :
	// in "TabWidget::addTab", under "Position tab's window", the widget is
	// moved up by 1 pixel
	m_tabWidget->setMinimumHeight( INSTRUMENT_HEIGHT + GRAPHIC_TAB_HEIGHT - 4 - 1 );


	// create tab-widgets
	m_ssView = new InstrumentSoundShapingView( m_tabWidget );

	// FUNC tab
	m_instrumentFunctionsView = new QWidget(m_tabWidget);
	auto instrumentFunctionsLayout = new QVBoxLayout(m_instrumentFunctionsView);
	instrumentFunctionsLayout->setContentsMargins(5, 5, 5, 5);
	m_noteStackingView = new InstrumentFunctionNoteStackingView( &m_track->m_noteStacking );
	m_arpeggioView = new InstrumentFunctionArpeggioView( &m_track->m_arpeggio );

	instrumentFunctionsLayout->addWidget( m_noteStackingView );
	instrumentFunctionsLayout->addWidget( m_arpeggioView );
	instrumentFunctionsLayout->addStretch();

	// MIDI tab
	m_midiView = new InstrumentMidiIOView(m_tabWidget);

	// FX tab
	m_effectView = new EffectRackView(m_track->m_audioBusHandle.effects(), m_tabWidget);

	// Tuning tab
	m_tuningView = new InstrumentTuningView(m_track, m_tabWidget);


	m_tabWidget->addTab(m_ssView, tr("Envelope, filter & LFO"), "env_lfo_tab", 1);
	m_tabWidget->addTab(m_instrumentFunctionsView, tr("Chord stacking & arpeggio"), "func_tab", 2);
	m_tabWidget->addTab(m_effectView, tr("Effects"), "fx_tab", 3);
	m_tabWidget->addTab(m_midiView, tr("MIDI"), "midi_tab", 4);
	m_tabWidget->addTab(m_tuningView, tr("Tuning and transposition"), "tuning_tab", 5);

	// setup piano-widget
	m_pianoView = new PianoView( this );
	m_pianoView->setMinimumHeight( PIANO_HEIGHT );
	m_pianoView->setMaximumHeight( PIANO_HEIGHT );
	m_pianoView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	m_recordButton = new QToolButton(this);
	m_recordButton->setObjectName("recordButton");
	m_recordButton->setText(tr("REC"));
	m_recordButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
	m_recordButton->setCheckable(true);
	m_recordButton->setFixedSize(34, 18);
	m_recordButton->setAccessibleName(tr("Record MIDI notes"));
	m_recordButton->setToolTip(tr("Record MIDI notes to this instrument track. Right-click for start options."));
	m_recordButton->setStyleSheet(
		"QToolButton { border: 1px solid palette(mid); border-radius: 2px; padding: 0; font-size: 8px; }"
		"QToolButton:checked { background-color: #d32f2f; color: white; }");
	m_recordButton->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(m_recordButton, &QToolButton::clicked, this, [this]
	{
		if (getGUI()->pianoRoll()->isRecordingTrack(m_track))
		{
			cancelArmedRecording();
			m_recordTriggerNoteKey = -1;
			m_recordTriggerNoteChannel = -1;
			getGUI()->pianoRoll()->stop();
		}
		else if (m_recordStartTimer.isActive() || m_waitingForInput) { cancelArmedRecording(); }
		else { startRecording(); }
		updateRecordingControls();
	});
	connect(m_recordButton, &QToolButton::customContextMenuRequested, this, [this](const QPoint& pos)
	{
		if (getGUI()->pianoRoll()->isRecording()) { return; }
		QMenu menu(m_recordButton);
		auto* delayed = menu.addAction(tr("Start recording in 3 seconds"));
		auto* onInput = menu.addAction(tr("Start recording when a note is played"));
		connect(delayed, &QAction::triggered, this, [this] { armRecording(false); });
		connect(onInput, &QAction::triggered, this, [this] { armRecording(true); });
		menu.exec(m_recordButton->mapToGlobal(pos));
	});
	m_recordStartTimer.setSingleShot(true);
	connect(&m_recordStartTimer, &QTimer::timeout, this, [this] { startRecording(); });

	m_recordChannelComboBox = new QComboBox(this);
	m_recordChannelComboBox->setObjectName("recordChannelComboBox");
	m_recordChannelComboBox->addItem(tr("All channels"), 0);
	for (int channel = 1; channel <= MidiChannelCount; ++channel)
	{
		m_recordChannelComboBox->addItem(tr("Channel %1").arg(channel), channel);
	}
	m_recordChannelComboBox->setToolTip(tr("MIDI channel for recorded notes; All channels preserves the incoming channel."));
	m_recordChannelComboBox->setAccessibleName(tr("MIDI recording channel"));
	QFont channelFont = m_recordChannelComboBox->font();
	if (channelFont.pointSizeF() > 2) { channelFont.setPointSizeF(channelFont.pointSizeF() - 1); }
	else if (channelFont.pixelSize() > 2) { channelFont.setPixelSize(channelFont.pixelSize() - 1); }
	m_recordChannelComboBox->setFont(channelFont);
	m_recordChannelComboBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
	m_recordChannelComboBox->setMinimumWidth(m_recordChannelComboBox->sizeHint().width());
	m_recordChannelComboBox->setFixedHeight(20);
	connect(m_recordChannelComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this,
		[this](int index) { setRecordChannel(m_recordChannelComboBox->itemData(index).toInt()); });

	auto pianoSection = new QWidget(this);
	pianoSection->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	auto pianoSectionLayout = new QVBoxLayout(pianoSection);
	pianoSectionLayout->setContentsMargins(0, 0, 0, 0);
	pianoSectionLayout->setSpacing(0);

	auto recordControls = new QWidget(pianoSection);
	recordControls->setFixedHeight(PIANO_RECORD_CONTROLS_HEIGHT);
	auto recordControlsLayout = new QHBoxLayout(recordControls);
	recordControlsLayout->setContentsMargins(4, 1, 4, 1);
	recordControlsLayout->setSpacing(4);
	recordControlsLayout->addWidget(m_recordButton);
	recordControlsLayout->addStretch(1);
	auto recordChannelLabel = new QLabel(tr("MIDI ch."), recordControls);
	recordChannelLabel->setBuddy(m_recordChannelComboBox);
	recordChannelLabel->setStyleSheet("font-size: 9px; color: palette(mid);");
	recordChannelLabel->setToolTip(m_recordChannelComboBox->toolTip());
	recordControlsLayout->addWidget(recordChannelLabel);
	recordControlsLayout->addWidget(m_recordChannelComboBox);

	pianoSectionLayout->addWidget(recordControls);
	pianoSectionLayout->addWidget(m_pianoView);

	// setup sizes and policies
	generalSettingsWidget->setMaximumHeight(90);
	generalSettingsWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	m_tabWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

	vlayout->addWidget( generalSettingsWidget );
	// Use QWidgetItem explicitly to make the size hint change on instrument changes
	// QLayout::addWidget() uses QWidgetItemV2 with size hint caching
	vlayout->insertItem(1, new QWidgetItem(m_tabWidget));
	vlayout->addWidget(pianoSection);
	setModel( _itv->model() );
	updateRecordingControls();
	connect(getGUI()->mainWindow(), &MainWindow::periodicUpdate,
		this, &InstrumentTrackWindow::updateRecordingControls);
	connect(m_track, &InstrumentTrack::midiNoteOn, this,
		[this](const Note& note)
	{
		if (!m_waitingForInput) { return; }
		m_waitingForInput = false;
		m_recordTriggerNoteKey = note.key();
		m_recordTriggerNoteChannel = note.midiChannel();
		auto pianoRoll = getGUI()->pianoRoll();
		if (pianoRoll->recordTrack(m_track))
		{
			pianoRoll->recordInputNoteOn(note);
		}
		else
		{
			m_recordTriggerNoteKey = -1;
			m_recordTriggerNoteChannel = -1;
		}
		updateRecordingControls();
	}, Qt::QueuedConnection);
	connect(m_track, &InstrumentTrack::midiNoteOff, this,
		[this](const Note& note)
	{
		if (m_recordTriggerNoteKey != note.key() || m_recordTriggerNoteChannel != note.midiChannel()) { return; }
		m_recordTriggerNoteKey = -1;
		m_recordTriggerNoteChannel = -1;
		auto pianoRoll = getGUI()->pianoRoll();
		if (pianoRoll->isRecordingTrack(m_track)) { pianoRoll->recordInputNoteOff(note); }
	}, Qt::QueuedConnection);

	QMdiSubWindow* subWin = getGUI()->mainWindow()->addWindowedWidget( this );

	updateInstrumentView();

	// The previous call should have given us a sub window parent. Therefore
	// we can reuse this method.
	updateSubWindow();

	subWin->hide();
}

void InstrumentTrackWindow::resizeEvent(QResizeEvent * event) {
	QWidget::resizeEvent(event);
	adjustTabSize(m_instrumentView);
	adjustTabSize(m_instrumentFunctionsView);
	adjustTabSize(m_ssView);
	adjustTabSize(m_effectView);
	adjustTabSize(m_midiView);
	adjustTabSize(m_tuningView);
}



InstrumentTrackWindow::~InstrumentTrackWindow()
{
	delete m_instrumentView;

	if (parentWidget())
	{
		parentWidget()->hide();
		parentWidget()->deleteLater();
	}
}




void InstrumentTrackWindow::setInstrumentTrackView( InstrumentTrackView* view )
{
	if( m_itv && view )
	{
		m_itv->m_tlb->setChecked( false );
	}

	m_itv = view;
	m_mixerChannelNumber->setTrackView(m_itv);
}




void InstrumentTrackWindow::modelChanged()
{
	m_track = castModel<InstrumentTrack>();

	m_nameLineEdit->setText( m_track->name() );

	m_track->disconnect( SIGNAL(nameChanged()), this );
	m_track->disconnect( SIGNAL(instrumentChanged()), this );

	connect( m_track, SIGNAL(nameChanged()),
			this, SLOT(updateName()));
	connect( m_track, SIGNAL(instrumentChanged()),
			this, SLOT(updateInstrumentView()));

	m_volumeKnob->setModel( &m_track->m_volumeModel );
	m_panningKnob->setModel( &m_track->m_panningModel );
	m_mixerChannelNumber->setModel( &m_track->m_mixerChannelModel );
	m_channelLayoutComboBox->setModel( m_track->channelLayoutModel() );
	m_pianoView->setModel( &m_track->m_piano );
	{
		const QSignalBlocker blocker(m_recordChannelComboBox);
		m_recordChannelComboBox->setCurrentIndex(
			m_recordChannelComboBox->findData(m_track->midiPort()->outputChannel()));
	}

	if (m_track->instrument() && m_track->instrument()->isBendable())
	{
		m_pitchKnob->setModel( &m_track->m_pitchModel );
		m_pitchRangeSpinBox->setModel( &m_track->m_pitchRangeModel );
		m_pitchKnob->show();
		m_pitchLabel->show();
		m_pitchRangeSpinBox->show();
		m_pitchRangeLabel->show();
	}
	else
	{
		m_pitchKnob->hide();
		m_pitchLabel->hide();
		m_pitchKnob->setModel( nullptr );
		m_pitchRangeSpinBox->hide();
		m_pitchRangeLabel->hide();
	}

	if (m_track->instrument() && m_track->instrument()->isMidiBased())
	{
		m_tuningView->microtunerNotSupportedLabel()->show();
		m_tuningView->microtunerGroupBox()->hide();
		m_track->m_microtuner.enabledModel()->setValue(false);
	}
	else
	{
		m_tuningView->microtunerNotSupportedLabel()->hide();
		m_tuningView->microtunerGroupBox()->show();
	}

	m_ssView->setModel(&m_track->m_soundShaping);
	m_noteStackingView->setModel(&m_track->m_noteStacking);
	m_arpeggioView->setModel(&m_track->m_arpeggio);
	m_midiView->setModel(&m_track->m_midiPort);
	m_effectView->setModel(m_track->m_audioBusHandle.effects());
	m_tuningView->pitchGroupBox()->setModel(&m_track->m_useMasterPitchModel);
	m_tuningView->microtunerGroupBox()->setModel(m_track->m_microtuner.enabledModel());
	m_tuningView->scaleCombo()->setModel(m_track->m_microtuner.scaleModel());
	m_tuningView->keymapCombo()->setModel(m_track->m_microtuner.keymapModel());
	m_tuningView->rangeImportCheckbox()->setModel(m_track->m_microtuner.keyRangeImportModel());
	updateName();

	updateSubWindow();
}


void InstrumentTrackWindow::setRecordChannel(int channel)
{
	if (m_track) { m_track->midiPort()->setOutputChannel(channel); }
}


void InstrumentTrackWindow::startRecording()
{
	cancelArmedRecording();
	getGUI()->pianoRoll()->recordTrack(m_track);
	updateRecordingControls();
}


void InstrumentTrackWindow::armRecording(bool waitForInput)
{
	cancelArmedRecording();
	m_waitingForInput = waitForInput;
	if (!waitForInput) { m_recordStartTimer.start(3000); }
	updateRecordingControls();
}


void InstrumentTrackWindow::cancelArmedRecording()
{
	m_recordStartTimer.stop();
	m_waitingForInput = false;
}


void InstrumentTrackWindow::updateRecordingControls()
{
	auto pianoRoll = getGUI()->pianoRoll();
	const auto outputChannel = m_track->midiPort()->outputChannel();
	if (m_recordChannelComboBox->currentIndex() != outputChannel)
	{
		const QSignalBlocker blocker(m_recordChannelComboBox);
		m_recordChannelComboBox->setCurrentIndex(outputChannel);
	}
	const bool recording = pianoRoll->isRecordingTrack(m_track);
	const bool armed = m_recordStartTimer.isActive() || m_waitingForInput;
	if (!recording && !m_waitingForInput)
	{
		m_recordTriggerNoteKey = -1;
		m_recordTriggerNoteChannel = -1;
	}
	if (m_recordButton->isChecked() != (recording || armed))
	{
		const QSignalBlocker blocker(m_recordButton);
		m_recordButton->setChecked(recording || armed);
	}
	m_recordButton->setText(recording ? tr("STOP") : armed ? tr("ARM") : tr("REC"));
	m_recordButton->setToolTip(recording ? tr("Stop recording this instrument track") : armed ?
		tr("Recording is armed; click to cancel. Right-click to change start mode.") :
		tr("Record MIDI notes to this instrument track. Right-click for delayed or input-triggered start."));
	m_recordButton->setEnabled(recording || !pianoRoll->isRecording());
}




void InstrumentTrackWindow::saveSettingsBtnClicked()
{
	FileDialog sfd(this, tr("Save preset"), "", tr("XML preset file (*.xpf)"));

	QString presetRoot = ConfigManager::inst()->userPresetsDir();
	if(!QDir(presetRoot).exists())
	{
		QDir().mkdir(presetRoot);
	}
	if(!QDir(presetRoot + m_track->instrumentName()).exists())
	{
		QDir(presetRoot).mkdir(m_track->instrumentName());
	}

	sfd.setAcceptMode(FileDialog::AcceptSave);
	sfd.setDirectory(presetRoot + m_track->instrumentName());
	sfd.setFileMode( FileDialog::AnyFile );
	QString fname = m_track->name();
	sfd.selectFile(fname.remove(QRegularExpression(FILENAME_FILTER)));
	sfd.setDefaultSuffix( "xpf");

	if( sfd.exec() == QDialog::Accepted &&
		!sfd.selectedFiles().isEmpty() &&
		!sfd.selectedFiles().first().isEmpty() )
	{
		DataFile dataFile(DataFile::Type::InstrumentTrackSettings);
		QDomElement& content(dataFile.content());

		m_track->savePreset(dataFile, content);
		//We don't want to save muted & solo settings when we're saving a preset
		content.setAttribute("muted", 0);
		content.setAttribute("solo", 0);
		content.setAttribute("mutedBeforeSolo", 0);
		QString f = sfd.selectedFiles()[0];
		dataFile.writeFile(f);
	}
}





void InstrumentTrackWindow::updateName()
{
	setWindowTitle( m_track->name().length() > 25 ? ( m_track->name().left(24)+"..." ) : m_track->name() );

	if( m_nameLineEdit->text() != m_track->name() )
	{
		m_nameLineEdit->setText( m_track->name() );
	}
}





void InstrumentTrackWindow::updateInstrumentView()
{
	delete m_instrumentView;
	if( m_track->m_instrument != nullptr )
	{
		m_instrumentView = m_track->m_instrument->createView( m_tabWidget );
		m_instrumentViewAspect = m_instrumentView->sizeHint().expandedTo(m_instrumentView->minimumSizeHint())
			.expandedTo(m_instrumentView->minimumSize());
		if (!m_instrumentViewAspect.isValid() || m_instrumentViewAspect.isEmpty())
		{
			m_instrumentViewAspect = QSize(INSTRUMENT_WIDTH, INSTRUMENT_HEIGHT);
		}
		m_tabWidget->addTab( m_instrumentView, tr( "Plugin" ), "plugin_tab", 0 );
		m_tabWidget->setActiveTab( 0 );

		m_tabWidget->setMinimumSize(m_instrumentViewAspect.width() + 4,
			m_instrumentViewAspect.height() + GRAPHIC_TAB_HEIGHT - 1);

		m_ssView->setFunctionsHidden(m_track->m_instrument->isSingleStreamed());

		modelChanged(); 		// Get the instrument window to refresh
		m_track->dataChanged(); // Get the text on the trackButton to change

		adjustTabSize(m_instrumentView);
		m_pianoView->setVisible(m_track->m_instrument->hasNoteInput());
		// adjust window size
		layout()->invalidate();
		resize(sizeHint());
		if (parentWidget())
		{
			parentWidget()->resize(parentWidget()->sizeHint());
		}
		update();
		m_instrumentView->update();
	}
}




void InstrumentTrackWindow::textChanged( const QString& newName )
{
	m_track->setName( newName );
	Engine::getSong()->setModified();
}




void InstrumentTrackWindow::toggleVisibility( bool on )
{
	if( on )
	{
		show();
		parentWidget()->show();
		parentWidget()->raise();
	}
	else
	{
		parentWidget()->hide();
		hide();
	}
}




void InstrumentTrackWindow::closeEvent( QCloseEvent* event )
{
	m_itv->setFocus();
	m_itv->m_tlb->setChecked(false);
}




void InstrumentTrackWindow::focusInEvent( QFocusEvent* )
{
	if(m_pianoView->isVisible()) {
		m_pianoView->setFocus();
	}
}




void InstrumentTrackWindow::dragEnterEventGeneric( QDragEnterEvent* event )
{
	StringPairDrag::processDragEnterEvent( event, "instrument,presetfile,pluginpresetfile" );
}




void InstrumentTrackWindow::dragEnterEvent( QDragEnterEvent* event )
{
	dragEnterEventGeneric( event );
}




void InstrumentTrackWindow::dropEvent( QDropEvent* event )
{
	QString type = StringPairDrag::decodeKey( event );
	QString value = StringPairDrag::decodeValue( event );

	if( type == "instrument" )
	{
		m_track->loadInstrument( value, nullptr, true /* DnD */ );

		Engine::getSong()->setModified();

		event->accept();
		setFocus();
	}
	else if( type == "presetfile" )
	{
		DataFile dataFile(value);
		m_track->replaceInstrument(dataFile);
		event->accept();
		setFocus();
	}
	else if( type == "pluginpresetfile" )
	{
		const QString ext = FileItem::extension( value );
		Instrument * i = m_track->instrument();

		if( !i->descriptor()->supportsFileType( ext ) )
		{
			PluginFactory::PluginInfoAndKey piakn =
				getPluginFactory()->pluginSupportingExtension(ext);
			i = m_track->loadInstrument(piakn.info.name(), &piakn.key);
		}

		i->loadFile( value );

		event->accept();
		setFocus();
	}
	updateSubWindow();
}




void InstrumentTrackWindow::saveSettings( QDomDocument& doc, QDomElement & thisElement )
{
	thisElement.setAttribute( "tab", m_tabWidget->activeTab() );
	MainWindow::saveWidgetState( this, thisElement );
}




void InstrumentTrackWindow::loadSettings( const QDomElement& thisElement )
{
	m_tabWidget->setActiveTab( thisElement.attribute( "tab" ).toInt() );
	MainWindow::restoreWidgetState( this, thisElement );
	if( isVisible() )
	{
		m_itv->m_tlb->setChecked( true );
	}
}

void InstrumentTrackWindow::viewInstrumentInDirection(int d)
{
	// helper routine for viewNextInstrument, viewPrevInstrument
	// d=-1 to view the previous instrument,
	// d=+1 to view the next instrument

	const QList<TrackView *> &trackViews = m_itv->trackContainerView()->trackViews();
	int idxOfMe = trackViews.indexOf(m_itv);

	// search for the next InstrumentTrackView (i.e. skip AutomationViews, etc)
	// sometimes, the next InstrumentTrackView may already be open, in which case
	//   replace our window contents with the *next* closed Instrument Track and
	//   give focus to the InstrumentTrackView we skipped.
	int idxOfNext = idxOfMe;
	InstrumentTrackView *newView = nullptr;
	InstrumentTrackView *bringToFront = nullptr;
	do
	{
		idxOfNext = (idxOfNext + d + trackViews.size()) % trackViews.size();
		newView = dynamic_cast<InstrumentTrackView*>(trackViews[idxOfNext]);
		// the window that should be brought to focus is the FIRST InstrumentTrackView that comes after us
		if (bringToFront == nullptr && newView != nullptr)
		{
			bringToFront = newView;
		}
		// if the next instrument doesn't have an active window, then exit loop & load that one into our window.
		if (newView != nullptr && !newView->m_tlb->isChecked())
		{
			break;
		}
	} while (idxOfNext != idxOfMe);

	// avoid reloading the window if there is only one instrument, as that will just change the active tab
	if (idxOfNext != idxOfMe)
	{
		const auto sourceSubwin = static_cast<SubWindow*>(parentWidget());
		const auto targetSubwin = static_cast<SubWindow*>(newView->getInstrumentTrackWindow()->parentWidget());
		QWidget* sourceWidget;
		QWidget* targetWidget;

		// set widgets we move and get our position from
		if (sourceSubwin->isDetached())
		{
			sourceWidget = this;
			targetWidget = newView->getInstrumentTrackWindow();
		}
		else
		{
			sourceWidget = parentWidget();
			targetWidget = newView->getInstrumentTrackWindow()->parentWidget();
		}

		// save current window pos and then hide the window by unchecking its button in the track list
		QPoint curPos = sourceWidget->pos();
		m_itv->m_tlb->setChecked(false);

		// enable the new window by checking its track list button & moving it to where our window just was
		newView->m_tlb->setChecked(true);

		// sync detached state with current widget like we do with position
		targetSubwin->setDetached(sourceSubwin->isDetached());

		targetWidget->move(curPos);

		// scroll the SongEditor/PatternEditor to make sure the new trackview label is visible
		bringToFront->trackContainerView()->scrollToTrackView(bringToFront);

		// get the instrument window to refresh
		modelChanged();
	}
	Q_ASSERT(bringToFront);
	bringToFront->getInstrumentTrackWindow()->setFocus();

	updateSubWindow();
}

void InstrumentTrackWindow::viewNextInstrument()
{
	viewInstrumentInDirection(+1);
}
void InstrumentTrackWindow::viewPrevInstrument()
{
	viewInstrumentInDirection(-1);
}

void InstrumentTrackWindow::adjustTabSize(QWidget *w)
{
	if (!w) { return; }
	const QSize available(std::max(1, m_tabWidget->width() - 4),
		std::max(1, m_tabWidget->height() - GRAPHIC_TAB_HEIGHT));
	if (w == m_instrumentView)
	{
		QSize pluginSize = m_instrumentViewAspect;
		if (m_instrumentView->isResizable())
		{
			pluginSize.scale(available.boundedTo(m_instrumentView->maximumSize()), Qt::KeepAspectRatio);
			pluginSize = pluginSize.expandedTo(m_instrumentView->minimumSize());
		}
		w->resize(pluginSize);
		w->move(2 + std::max(0, (available.width() - w->width()) / 2), GRAPHIC_TAB_HEIGHT - 1);
	}
	else
	{
		w->move(2, GRAPHIC_TAB_HEIGHT - 1);
		w->resize(available);
	}
	w->update();
}

QMdiSubWindow* InstrumentTrackWindow::findSubWindowInParents()
{
	// TODO Move to helper? Does not seem to be provided by Qt.
	auto p = parentWidget();

	while (p != nullptr)
	{
		auto mdiSubWindow = dynamic_cast<QMdiSubWindow*>(p);
		if (mdiSubWindow)
		{
			return mdiSubWindow;
		}
		else
		{
			p = p->parentWidget();
		}
	}

	return nullptr;
}

void InstrumentTrackWindow::updateSubWindow()
{
	auto subWindow = findSubWindowInParents();
	if (subWindow && m_instrumentView)
	{
		setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
		setMinimumSize(layout()->minimumSize());
		subWindow->setWindowFlag(Qt::WindowMaximizeButtonHint, true);

		// TODO This is only needed if the sub window is implemented with MXM's own SubWindow class.
		// If an QMdiSubWindow is used everything works automatically. It seems that SubWindow is
		// missing some implementation details that QMdiSubWindow has.
		auto subWin = dynamic_cast<SubWindow*>(subWindow);
		if (subWin)
		{
			subWin->updateTitleBar();
		}
	}
}

} // namespace mxm::gui
