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
		const auto inputChannels = engine->audioDev()->supportsExternalPatchbayInput()
			? ch_cnt_t{8} : ch_cnt_t{2};
		engine->registerTrackInput(m_trackInputId, name(), inputChannels);
		engine->setTrackInputSource(m_trackInputId, m_inputSourceId);
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
	auto inputSource = thisElem.attribute("input_source", "system-default");
	if (inputSource == QStringLiteral("auto") || inputSource.startsWith(QStringLiteral("device:")))
	{
		inputSource = QString::fromLatin1(SystemDefaultInputSourceId);
	}
	setInputAssignment(inputSource);
	setRecordChannel(thisElem.attribute("recordchannel", "0").toInt());
	setInputMonitoring(thisElem.attribute("inputmonitor", "0").toInt() != 0);
}


void SampleTrack::setRecordChannel(int channel)
{
	m_recordChannel.store(std::clamp(channel, 0, 2), std::memory_order_relaxed);
}




void SampleTrack::setInputAssignment(const QString& sourceId)
{
	const auto normalized = sourceId.trimmed().isEmpty()
		? QString::fromLatin1(SystemDefaultInputSourceId) : sourceId.trimmed();
	if (m_inputSourceId == normalized) { return; }
	if (isRecording())
	{
		if (Engine::getSong())
		{
			Engine::getSong()->collectError(tr("Cannot change the input for \"%1\" during a take.").arg(name()));
		}
		emit inputAssignmentChanged();
		return;
	}

	m_inputSourceId = normalized;
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




auto SampleTrack::lookupAssignedInput(QString& id) const -> std::optional<AudioInputSource>
{
	auto* engine = Engine::audioEngine();
	id = m_inputSourceId.trimmed().isEmpty()
		? QString::fromLatin1(SystemDefaultInputSourceId) : m_inputSourceId;
	if (!engine || id == QStringLiteral("none")) { return std::nullopt; }

	auto source = id == QLatin1String(TrackInputSourceId)
		? (m_trackInputId != 0 ? engine->trackInputSource(
			m_trackInputId, tr("Track input port: %1").arg(name())) : std::nullopt)
		: engine->inputSource(id);
	return source && source->valid() ? source : std::nullopt;
}


auto SampleTrack::resolvedInputSource() const -> std::optional<AudioInputSource>
{
	if (!Engine::audioEngine()) { return std::nullopt; }
	QString id;
	return lookupAssignedInput(id);
}




auto SampleTrack::inputAssignmentStatus() const -> QString
{
	auto* engine = Engine::audioEngine();
	if (!engine)
	{
		return tr("No audio capture inputs are available.");
	}

	QString id;
	const auto source = lookupAssignedInput(id);
	if (id == QStringLiteral("none")) { return tr("Select an input source before recording."); }
	if (!source)
	{
		if (engine->inputSources().empty())
		{
			return tr("No audio capture inputs are available.");
		}
		return tr("Saved input \"%1\" is unavailable. Select another input before recording.").arg(id);
	}
	if (engine->trackInputCaptureFailed(m_trackInputId))
	{
		return tr("The assigned input could not be opened.");
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
	if (!source || engine->trackInputCaptureFailed(m_trackInputId))
	{
		if (Engine::getSong())
		{
			Engine::getSong()->collectError(
				tr("Cannot record on \"%1\": %2").arg(name(), inputAssignmentStatus()));
		}
		return false;
	}
	// Snapshot the physical assignment at take start. Device or assignment
	// changes mid-take must never silently remap the recording to another source.
	auto takeSource = *source;
	if (takeSource.channelsFollowCapture)
	{
		const auto channels = engine->trackInputChannels(m_trackInputId);
		if (channels == 0)
		{
			if (Engine::getSong())
			{
				Engine::getSong()->collectError(tr("Cannot record on \"%1\": the input channel layout is unavailable.")
					.arg(name()));
			}
			return false;
		}
		takeSource.channels = channels;
		takeSource.layout = channels == 1 ? AudioChannelLayout::mono()
			: channels == 2 ? AudioChannelLayout::stereo() : AudioChannelLayout::discrete(channels);
	}
	auto firstInputChannel = ch_cnt_t{0};
	auto destinationChannels = std::vector<ch_cnt_t>{};
	auto captureLayout = AudioChannelLayout{};

	if (recordChannel() == 1 || recordChannel() == 2)
	{
		firstInputChannel = std::min<ch_cnt_t>(
			static_cast<ch_cnt_t>(recordChannel() - 1), source->channels - 1);
		takeSource.channels = 1;
		takeSource.layout = AudioChannelLayout::mono();
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
	m_recordingSession->engineDroppedAtStart = engine->trackInputDroppedFrames(m_trackInputId);
	m_recordingSession->source = std::move(takeSource);
	m_recordingSession->destinationChannels = std::move(destinationChannels);
	m_recordingSession->trackInputId = m_trackInputId;
	m_recordingSession->firstInputChannel = firstInputChannel;
	m_recordingSession->capture = std::make_shared<SampleCapture>(
		engine->outputSampleRate(), captureLayout, engine->framesPerPeriod(), 64);
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
	const auto framesPerTick = Engine::framesPerTick(Engine::audioEngine()->outputSampleRate());
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
				// Keep the audio-thread guard out of the disk flush and WAV decode.
				{
					const auto guard = Engine::audioEngine()->requestChangesGuard();
				}
				recorded = m_recordingSession->capture->finish();
				const auto engineDropped = Engine::audioEngine()->trackInputDroppedFrames(
					m_recordingSession->trackInputId) - m_recordingSession->engineDroppedAtStart;
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
				const auto captureFailed = m_recordingSession->inputUnavailable.load(std::memory_order_relaxed)
					|| Engine::audioEngine()->trackInputCaptureFailed(m_recordingSession->trackInputId);
				if (captureFailed && Engine::getSong())
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

	const auto input = engine->trackInputBuffer(m_trackInputId);
	if (input.empty()) { return 0.f; }
	const auto channels = source->channelsFollowCapture ? input.channels() : source->channels;

	auto peak = 0.f;
	for (ch_cnt_t channel = 0; channel < channels; ++channel)
	{
		const auto* data = input.bufferPtr(channel);
		for (f_cnt_t frame = 0; frame < input.frames(); ++frame)
		{
			peak = std::max(peak, std::abs(data[frame]));
		}
	}
	return peak;
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
