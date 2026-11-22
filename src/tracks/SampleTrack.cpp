/*
 * SampleTrack.cpp - implementation of class SampleTrack, a track which
 *                   provides arrangement of samples
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

#include "SampleTrack.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QColor>
#include <QDomElement>
#include <QStringList>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "AudioInputSource.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Mixer.h"
#include "panning.h"
#include "PatternStore.h"
#include "PatternTrack.h"
#include "SampleClip.h"
#include "SamplePlayHandle.h"
#include "SampleCapture.h"
#include "SampleRecordHandle.h"
#include "SampleTrackView.h"
#include "Song.h"
#include "volume.h"


namespace mxm
{


SampleTrack::SampleTrack(TrackContainer* tc) :
	Track(Track::Type::Sample, tc),
	m_volumeModel(DefaultVolume, MinVolume, MaxVolume, 0.1f, this, tr("Volume")),
	m_panningModel(DefaultPanning, PanningLeft, PanningRight, 0.1f, this, tr("Panning")),
	m_mixerChannelModel(0, 0, 0, this, tr("Mixer channel")),
	m_audioBusHandle(tr("Sample track"), true, &m_volumeModel, &m_panningModel, &m_mutedModel),
	m_isPlaying(false)
{
	setName(tr("Sample track"));
	m_panningModel.setCenterValue(DefaultPanning);
	m_mixerChannelModel.setRange(0, Engine::mixer()->numChannels()-1, 1);

	connect(&m_mixerChannelModel, SIGNAL(dataChanged()), this, SLOT(updateMixerChannel()));

	auto* engine = Engine::audioEngine();
	if (engine && engine->audioDev() && engine->audioDev()->supportsTrackInputPorts())
	{
		m_trackInputId = static_cast<quint64>(reinterpret_cast<std::uintptr_t>(this));
		engine->registerTrackInput(m_trackInputId, name(), 2);
	}
}




SampleTrack::~SampleTrack()
{
	stopRecording(false);
	if (m_trackInputId != 0 && Engine::audioEngine())
	{
		Engine::audioEngine()->unregisterTrackInput(m_trackInputId);
	}
	Engine::audioEngine()->removePlayHandlesOfTypes(this,
		PlayHandle::Type::SamplePlayHandle | PlayHandle::Type::SampleRecordPlayHandle);
}




bool SampleTrack::play( const TimePos & _start, const f_cnt_t _frames,
					const f_cnt_t _offset, int _clip_num )
{
	bool played_a_note = false; // will be return variable


	clipVector clips;
	class PatternTrack * pattern_track = nullptr;
	if( _clip_num >= 0 )
	{
		auto sClip = dynamic_cast<SampleClip*>(getClip(_clip_num));

		if (_start > sClip->length())
		{
			setPlaying(false);
		}
		if (sClip->isPlaying())
		{
			return false;
		}
		clips.push_back(sClip);
		if (trackContainer() == Engine::patternStore())
		{
			auto bufferFramesPerTick = Engine::framesPerTick(sClip->sample().sampleRate());
			f_cnt_t sampleStart = bufferFramesPerTick * _start;
			pattern_track = PatternTrack::findPatternTrack(_clip_num);
			sClip->setSampleStartFrame(sampleStart);
			sClip->setIsPlaying(true);
			setPlaying(true);
		}
	}
	else
	{
		bool nowPlaying = false;
		for( int i = 0; i < numOfClips(); ++i )
		{
			Clip * clip = getClip( i );
			auto sClip = dynamic_cast<SampleClip*>(clip);

			if( _start >= sClip->startPosition() && _start < sClip->endPosition() )
			{
				if (sClip->isPlaying() == false && _start >= (sClip->startPosition() + sClip->startTimeOffset()))
				{
					auto bufferFramesPerTick = Engine::framesPerTick(sClip->sample().sampleRate());
					f_cnt_t sampleStart = bufferFramesPerTick *
						(_start - sClip->startPosition() - sClip->startTimeOffset());
					f_cnt_t clipFrameLength = bufferFramesPerTick * ( sClip->endPosition() - sClip->startPosition() - sClip->startTimeOffset() );
					f_cnt_t sampleBufferLength = sClip->sample().sampleSize();
					//if the Clip smaller than the sample length we play only until Clip end
					//else we play the sample to the end but nothing more
					f_cnt_t samplePlayLength = clipFrameLength > sampleBufferLength ? sampleBufferLength : clipFrameLength;
					if (sampleStart < sampleBufferLength)
					{
						sClip->setSampleStartFrame( sampleStart );
						sClip->setSamplePlayLength( samplePlayLength );
						clips.push_back( sClip );
						sClip->setIsPlaying( true );
						nowPlaying = true;
					}
				}
			}
			else
			{
				sClip->setIsPlaying( false );
			}
			nowPlaying = nowPlaying || sClip->isPlaying();
		}
		setPlaying(nowPlaying);
	}

	for (const auto& clip : clips)
	{
		auto st = dynamic_cast<SampleClip*>(clip);
		if (!st->isMuted())
		{
			auto handle = new SamplePlayHandle(st);
			handle->setPatternTrack(pattern_track);
			handle->setOffset( _offset );
			// send it to the audio engine
			Engine::audioEngine()->addPlayHandle( handle );
			played_a_note = true;
		}
	}

	return played_a_note;
}




gui::TrackView * SampleTrack::createView( gui::TrackContainerView* tcv )
{
	return new gui::SampleTrackView( this, tcv );
}




Clip * SampleTrack::createClip(const TimePos & pos)
{
	auto sClip = new SampleClip(this);
	sClip->setColor(QColor::fromHsv((getClipNum(sClip) * 137) % 360, 170, 230));
	sClip->movePosition(pos);
	return sClip;
}




void SampleTrack::saveTrackSpecificSettings(QDomDocument& doc, QDomElement& thisElem, bool /*presetMode*/)
{
	m_audioBusHandle.effects()->saveState(doc, thisElem);
	m_volumeModel.saveSettings(doc, thisElem, "vol");
	m_panningModel.saveSettings(doc, thisElem, "pan");
	m_mixerChannelModel.saveSettings(doc, thisElem, "mixch");
	thisElem.setAttribute("input_source", m_inputSourceId);
	QStringList inputChannels;
	for (const auto channel : m_inputChannels) { inputChannels.append(QString::number(channel)); }
	if (!inputChannels.isEmpty()) { thisElem.setAttribute("input_channels", inputChannels.join(',')); }
	thisElem.setAttribute("recordchannel", recordChannel());
	thisElem.setAttribute("inputmonitor", isInputMonitoring());
}




void SampleTrack::loadTrackSpecificSettings(const QDomElement & thisElem)
{
	QDomNode node = thisElem.firstChild();
	m_audioBusHandle.effects()->clear();
	while(!node.isNull())
	{
		if (node.isElement())
		{
			if (m_audioBusHandle.effects()->nodeName() == node.nodeName())
			{
				m_audioBusHandle.effects()->restoreState(node.toElement());
			}
		}
		node = node.nextSibling();
	}
	m_volumeModel.loadSettings(thisElem, "vol");
	m_panningModel.loadSettings(thisElem, "pan");
	m_mixerChannelModel.setRange(0, Engine::mixer()->numChannels() - 1);
	m_mixerChannelModel.loadSettings(thisElem, "mixch");
	const auto inputSource = thisElem.attribute("input_source", "none");
	auto inputChannels = std::vector<ch_cnt_t>{};
	const auto inputChannelText = thisElem.attribute("input_channels");
	for (const auto& entry : inputChannelText.split(',', Qt::SkipEmptyParts))
	{
		bool ok = false;
		const auto channel = entry.trimmed().toUInt(&ok);
		if (ok && channel < MaxChannelsPerAudioBuffer)
		{
			inputChannels.push_back(static_cast<ch_cnt_t>(channel));
		}
	}
	setInputAssignment(inputSource == QStringLiteral("auto") ? QStringLiteral("none") : inputSource,
		std::move(inputChannels));
	setRecordChannel(thisElem.attribute("recordchannel", "0").toInt());
	setInputMonitoring(thisElem.attribute("inputmonitor", "0").toInt() != 0);
}


void SampleTrack::setRecordChannel(int channel)
{
	m_recordChannel.store(std::clamp(channel, 0, 2), std::memory_order_relaxed);
}




void SampleTrack::setInputAssignment(const QString& sourceId, std::vector<ch_cnt_t> channels)
{
	const auto normalized = sourceId.trimmed().isEmpty() ? QStringLiteral("auto") : sourceId.trimmed();
	if (m_inputSourceId == normalized && m_inputChannels == channels) { return; }

	m_inputSourceId = normalized;
	m_inputChannels = std::move(channels);
	if (m_trackInputId != 0 && Engine::audioEngine())
	{
		Engine::audioEngine()->setTrackInputSource(m_trackInputId, m_inputSourceId);
	}
	emit inputAssignmentChanged();
	if (Engine::getSong()) { Engine::getSong()->setModified(); }
}




auto SampleTrack::availableInputSources() const -> std::vector<AudioInputSource>
{
	auto sources = Engine::audioEngine() ? Engine::audioEngine()->inputSources()
		: std::vector<AudioInputSource>{};
	if (m_trackInputId != 0 && Engine::audioEngine())
	{
		if (const auto trackInput = Engine::audioEngine()->trackInputSource(
			m_trackInputId, tr("External patchbay input")))
		{
			sources.push_back(*trackInput);
		}
	}
	return sources;
}




auto SampleTrack::resolvedInputSource() const -> std::optional<AudioInputSource>
{
	auto* engine = Engine::audioEngine();
	if (!engine) { return std::nullopt; }

	const auto id = m_inputSourceId.trimmed().isEmpty() ? QStringLiteral("auto") : m_inputSourceId;
	if (id == QStringLiteral("none")) { return std::nullopt; }
	std::optional<AudioInputSource> source;
	if (id == QStringLiteral("track"))
	{
		if (m_trackInputId == 0) { return std::nullopt; }
		source = engine->trackInputSource(m_trackInputId, tr("Track input port: %1").arg(name()));
	}
	else
	{
		source = engine->inputSource(id);
	}
	if (!source) { return std::nullopt; }

	if (!validateInputSource(*source, m_inputChannels, engine->inputChannels()))
	{
		return std::nullopt;
	}
	return source;
}




auto SampleTrack::inputAssignmentStatus() const -> QString
{
	auto* engine = Engine::audioEngine();
	if (!engine || engine->inputSources().empty())
	{
		return tr("No audio capture inputs are available.");
	}

	const auto id = m_inputSourceId.trimmed().isEmpty() ? QStringLiteral("auto") : m_inputSourceId;
	if (id == QStringLiteral("none")) { return tr("Select an input source before recording."); }
	const auto source = id == QStringLiteral("track")
		? (m_trackInputId != 0 ? engine->trackInputSource(
			m_trackInputId, tr("Track input port: %1").arg(name())) : std::nullopt)
		: engine->inputSource(id);
	if (!source)
	{
		return tr("Saved input \"%1\" is unavailable. Select another input before recording.").arg(id);
	}
	if (!validateInputSource(*source, m_inputChannels, engine->inputChannels()))
	{
		return tr("The saved input channels for \"%1\" are unavailable.").arg(source->displayName);
	}
	return {};
}


bool SampleTrack::startRecording()
{
	if (isRecording()) { return true; }
	auto* engine = Engine::audioEngine();
	auto* audioDevice = engine ? engine->audioDev() : nullptr;
	if (!audioDevice || !audioDevice->supportsCapture())
	{
		if (Engine::getSong())
		{
			Engine::getSong()->collectError(tr("Cannot record: the active audio backend has no capture inputs."));
		}
		return false;
	}

	const auto source = resolvedInputSource();
	if (!source)
	{
		if (Engine::getSong())
		{
			Engine::getSong()->collectError(
				tr("Cannot record on \"%1\": %2").arg(name(), inputAssignmentStatus()));
		}
		return false;
	}
	const auto effectiveChannels = inputSourceChannels(*source, m_inputChannels);
	if (effectiveChannels.size() != source->channels)
	{
		if (Engine::getSong())
		{
			Engine::getSong()->collectError(tr("Cannot record on \"%1\": the input channel map is invalid.").arg(name()));
		}
		return false;
	}

	// Snapshot the physical assignment at take start. Device or assignment
	// changes mid-take must never silently remap the recording to another source.
	auto takeSource = *source;
	takeSource.captureChannels = effectiveChannels;
	auto destinationChannels = std::vector<ch_cnt_t>{};
	auto captureLayout = AudioChannelLayout{};

	if (recordChannel() == 1 || recordChannel() == 2)
	{
		const auto sourceIndex = std::min<std::size_t>(
			static_cast<std::size_t>(recordChannel() - 1), effectiveChannels.size() - 1);
		takeSource.channels = 1;
		takeSource.layout = AudioChannelLayout::mono();
		takeSource.captureChannels = {effectiveChannels[sourceIndex]};
		captureLayout = AudioChannelLayout::stereo();
		destinationChannels = {static_cast<ch_cnt_t>(recordChannel() - 1)};
	}
	else
	{
		captureLayout = takeSource.layout;
		if (captureLayout.channels() != takeSource.channels)
		{
			captureLayout = AudioChannelLayout::discrete(takeSource.channels);
			takeSource.layout = captureLayout;
		}
		destinationChannels.resize(takeSource.channels);
		for (ch_cnt_t channel = 0; channel < takeSource.channels; ++channel)
		{
			destinationChannels[channel] = channel;
		}
	}

	auto song = Engine::getSong();
	const auto playMode = song->playMode();
	const auto position = playMode == Song::PlayMode::None
		? song->getPlayPos(Song::PlayMode::None) : song->getPlayPos(Song::PlayMode::Song);
	auto clip = static_cast<SampleClip*>(createClip(position));
	if (m_recording.exchange(true, std::memory_order_relaxed)) { delete clip; return true; }

	m_recordingSession = std::make_shared<SampleRecordSession>();
	m_recordingSession->engineDroppedAtStart = engine->inputDroppedFrames();
	m_recordingSession->source = std::move(takeSource);
	m_recordingSession->destinationChannels = std::move(destinationChannels);
	m_recordingSession->trackInputId = source->trackPort ? m_trackInputId : 0;
	m_recordingSession->capture = std::make_shared<SampleCapture>(
		engine->inputSampleRate(), captureLayout, engine->framesPerPeriod(), 64);
	m_recordingClip = clip;
	clip->changeLength(TimePos(1));
	song->trackRecordingStarted();
	engine->addPlayHandle(new SampleRecordHandle(clip, m_recordingSession));
	song->startTrackRecordingTransport(position);
	return true;
}




void SampleTrack::updateRecordingClipLength()
{
	if (!isRecording() || !m_recordingClip) { return; }
	if (!m_recordingSession) { return; }
	const auto frames = m_recordingSession->recordedFrames.load(std::memory_order_relaxed);
	const auto framesPerTick = Engine::framesPerTick(Engine::audioEngine()->inputSampleRate());
	const auto ticks = std::min<double>(static_cast<double>(frames) / framesPerTick,
		std::numeric_limits<tick_t>::max());
	m_recordingClip->changeLength(TimePos(static_cast<tick_t>(ticks)));
}


void SampleTrack::stopRecording(bool stopTransport)
{
	if (m_recording.exchange(false, std::memory_order_relaxed))
	{
		std::shared_ptr<const SampleBuffer> recorded;
		if (m_recordingSession)
		{
			m_recordingSession->active.store(false, std::memory_order_relaxed);
			if (m_recordingSession->capture)
			{
				// Wait for an in-flight audio period to finish before the writer
				// is stopped, so no accepted block can be published afterwards.
				const auto guard = Engine::audioEngine()->requestChangesGuard();
				recorded = m_recordingSession->capture->finish();
				const auto engineDropped = Engine::audioEngine()->inputDroppedFrames()
					- m_recordingSession->engineDroppedAtStart;
				const auto dropped = m_recordingSession->capture->droppedFrames() + engineDropped;
				if (dropped > 0 && Engine::getSong())
				{
					Engine::getSong()->collectError(tr("Recording overflowed: %1 frames were dropped.")
						.arg(static_cast<qulonglong>(dropped)));
				}
				if (m_recordingSession->capture->diskError() && Engine::getSong())
				{
					Engine::getSong()->collectError(tr("Recording could not be written to disk: %1")
						.arg(m_recordingSession->capture->errorString()));
				}
				if (m_recordingSession->inputUnavailable.load(std::memory_order_relaxed)
					&& Engine::getSong())
				{
					Engine::getSong()->collectError(tr("Recording on \"%1\": the assigned input became unavailable; no other input was substituted.")
						.arg(name()));
				}
			}
		}

		if (m_recordingClip)
		{
			if (recorded && !recorded->empty()) { m_recordingClip->setSampleBuffer(std::move(recorded)); }
			else { delete m_recordingClip; }
		}
		m_recordingClip = nullptr;
		m_recordingSession.reset();
		if (auto song = Engine::getSong()) { song->trackRecordingStopped(stopTransport); }
	}
}


float SampleTrack::inputPeak() const
{
	auto* engine = Engine::audioEngine();
	if (!engine) { return 0.f; }

	const auto source = resolvedInputSource();
	if (!source) { return 0.f; }

	const auto input = source->trackPort && m_trackInputId != 0
		? engine->trackInputBuffer(m_trackInputId) : engine->inputBuffer();
	if (input.empty()) { return 0.f; }

	const auto& channels = inputSourceChannels(*source, m_inputChannels);
	auto peak = 0.f;
	for (const auto channel : channels)
	{
		if (channel >= input.channels()) { return 0.f; }
		const auto* data = input.bufferPtr(channel);
		for (f_cnt_t frame = 0; frame < input.frames(); ++frame)
		{
			peak = std::max(peak, std::abs(data[frame]));
		}
	}
	return peak;
}




void SampleTrack::updateInputLevel(float level)
{
	float current = m_inputLevel.load(std::memory_order_relaxed);
	while (current < level &&
		!m_inputLevel.compare_exchange_weak(current, level, std::memory_order_relaxed))
	{
	}
}




void SampleTrack::updateClips()
{
	Engine::audioEngine()->removePlayHandlesOfTypes( this, PlayHandle::Type::SamplePlayHandle );
	setPlayingClips( false );
}




void SampleTrack::setPlayingClips( bool isPlaying )
{
	for( int i = 0; i < numOfClips(); ++i )
	{
		Clip * clip = getClip( i );
		auto sClip = dynamic_cast<SampleClip*>(clip);
		sClip->setIsPlaying( isPlaying );
	}
}




void SampleTrack::setName(const QString& newName)
{
	Track::setName(newName);
	if (m_trackInputId != 0 && Engine::audioEngine())
	{
		Engine::audioEngine()->renameTrackInput(m_trackInputId, name());
	}
}




void SampleTrack::updateMixerChannel()
{
	m_audioBusHandle.setNextMixerChannel(m_mixerChannelModel.value());
}


} // namespace mxm
