/*
 * AudioEngine.cpp - device-independent audio engine for MXM
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

#include "AudioEngine.h"

#include <algorithm>

#include <QMetaObject>

#include "MixHelpers.h"

#include "mxmconfig.h"

#include "AudioEngineWorkerThread.h"
#include "AudioBusHandle.h"
#include "Hardware.h"
#include "Mixer.h"
#include "Song.h"
#include "EnvelopeAndLfoParameters.h"
#include "NotePlayHandle.h"
#include "ConfigManager.h"

// platform-specific audio-interface-classes
#include "AudioAlsa.h"
#include "AudioJack.h"
#include "AudioSndio.h"
#include "AudioPortAudio.h"
#include "AudioPulseAudio.h"
#include "AudioSdl.h"
#include "AudioDummy.h"

// platform-specific midi-interface-classes
#include "MidiAlsaRaw.h"
#include "MidiAlsaSeq.h"
#include "MidiJack.h"
#include "MidiSndio.h"
#include "MidiWinMM.h"
#include "MidiApple.h"
#include "MidiDummy.h"

#include "BufferManager.h"

namespace mxm
{

using LocklessListElement = LocklessList<PlayHandle*>::Element;

static thread_local bool s_renderingThread = false;

AudioEngine::AudioEngine(bool renderOnly)
	: m_renderOnly(renderOnly)
	, m_framesPerAudioBuffer(std::clamp(
		static_cast<f_cnt_t>(ConfigManager::inst()->value("audioengine", "framesperaudiobuffer",
		QString::number(DEFAULT_BUFFER_SIZE)).toUInt()),
		MINIMUM_BUFFER_SIZE, MAXIMUM_BUFFER_SIZE))
	, m_framesPerPeriod(std::min(m_framesPerAudioBuffer, DEFAULT_BUFFER_SIZE))
	, m_baseSampleRate(
		std::max(ConfigManager::inst()->value("audioengine", "samplerate").toInt(), SUPPORTED_SAMPLERATES.front()))
	, m_outputBufferRead(nullptr)
	, m_outputBufferWrite(nullptr)
	, m_outputBufferReadIndex(0)
	, m_workers()
	, m_numWorkers(QThread::idealThreadCount() - 1)
	, m_newPlayHandles(PlayHandle::MaxNumber)
	, m_masterGain(1.0f)
	, m_audioDev(nullptr)
	, m_oldAudioDev(nullptr)
	, m_audioDevStartFailed(false)
	, m_profiler()
	, m_clearSignal(false)
	, m_sanitizationEnabled(ConfigManager::inst()->value("audioengine", "sanitizemix", "1").toInt())
{
	BufferManager::init( m_framesPerPeriod );
	m_outputBufferRead = std::make_unique<SampleFrame[]>(m_framesPerPeriod);
	m_outputBufferWrite = std::make_unique<SampleFrame[]>(m_framesPerPeriod);


	for( int i = 0; i < m_numWorkers+1; ++i )
	{
		auto wt = new AudioEngineWorkerThread(this);
		if( i < m_numWorkers )
		{
			wt->start( QThread::TimeCriticalPriority );
		}
		m_workers.push_back( wt );
	}
}




AudioEngine::~AudioEngine()
{
	for( int w = 0; w < m_numWorkers; ++w )
	{
		m_workers[w]->quit();
	}

	AudioEngineWorkerThread::startAndWaitForJobs();

	for( int w = 0; w < m_numWorkers; ++w )
	{
		m_workers[w]->wait( 500 );
	}

	delete m_midiClient;
	delete m_audioDev;


}




void AudioEngine::initDevices()
{
	bool success_ful = false;
	if( m_renderOnly ) {
		m_audioDev = new AudioDummy( success_ful, this );
		m_audioDevName = AudioDummy::name();
		m_midiClient = new MidiDummy;
		m_midiClientName = MidiDummy::name();
	} else {
		m_audioDev = tryAudioDevices();
		m_midiClient = tryMidiClients();
	}
	// Loading audio device may have changed the sample rate
	emit sampleRateChanged();
}

bool AudioEngine::criticalXRuns() const
{
	return cpuLoad() >= 99 && Engine::getSong()->isExporting() == false;
}




auto AudioEngine::inputSources() const -> std::vector<AudioInputSource>
{
	return m_audioDev ? m_audioDev->inputSources() : std::vector<AudioInputSource>{};
}

auto AudioEngine::inputSource(const QString& id) const -> std::optional<AudioInputSource>
{
	const auto sources = inputSources();
	const auto found = std::ranges::find_if(sources, [&id](const AudioInputSource& source) {
		return source.id == id;
	});
	if (found == sources.end()) { return std::nullopt; }
	return *found;
}

void AudioEngine::setTrackInputSource(quint64 id, const QString& sourceId)
{
	if (!m_audioDev || !m_audioDev->supportsTrackInputPorts()) { return; }
	// "track" means leave the ports unconnected for an external patchbay.
	// On device backends that same empty endpoint list closes the capture stream.
	if (sourceId == QLatin1String(TrackInputSourceId))
	{
		if (m_audioDev->supportsExternalPatchbayInput())
		{
			m_audioDev->setTrackInputSource(id, {});
		}
		return;
	}
	const auto source = inputSource(sourceId);
	m_audioDev->setTrackInputSource(id,
		source ? source->endpointPorts : QStringList{});
}




void AudioEngine::registerTrackInput(quint64 id, const QString& name, ch_cnt_t channels)
{
	if (id == 0 || channels == 0) { return; }

	const auto guard = requestChangesGuard();
	auto registered = false;
	{
		const auto lock = std::lock_guard{m_trackInputMutex};
		for (auto& slot : m_trackInputs)
		{
			if (slot.id.load(std::memory_order_acquire) == id) { return; }
		}

		for (auto& slot : m_trackInputs)
		{
			if (slot.id.load(std::memory_order_acquire) != 0) { continue; }

			const auto blockFrames = std::max<f_cnt_t>(m_framesPerPeriod, 1);
			slot.channels = channels;
			slot.queue = std::make_unique<AudioBuffer>(blockFrames * InputCaptureBlocks, channels);
			slot.snapshot = std::make_unique<AudioBuffer>(blockFrames * InputCaptureBlocks, channels);
			slot.blockFrames.assign(InputCaptureBlocks, 0);
			slot.queueBlockFrames = blockFrames;
			slot.snapshotFrames = 0;
			slot.readIndex.store(0, std::memory_order_relaxed);
			slot.writeIndex.store(0, std::memory_order_relaxed);
			slot.droppedFrames.store(0, std::memory_order_relaxed);
			slot.captureFailed.store(false, std::memory_order_relaxed);
			slot.id.store(id, std::memory_order_release);
			registered = true;
			break;
		}
	}
	if (registered && m_audioDev && m_audioDev->supportsTrackInputPorts()
		&& !m_audioDev->registerTrackInputPort(id, name, channels))
	{
		unregisterTrackInput(id);
	}
}




void AudioEngine::unregisterTrackInput(quint64 id)
{
	const auto guard = requestChangesGuard();
	if (m_audioDev) { m_audioDev->unregisterTrackInputPort(id); }
	{
		const auto lock = std::lock_guard{m_trackInputMutex};
		for (auto& slot : m_trackInputs)
		{
			if (slot.id.load(std::memory_order_acquire) != id) { continue; }
			slot.id.store(0, std::memory_order_release);
			slot.queue.reset();
			slot.snapshot.reset();
			slot.blockFrames.clear();
			slot.channels = 0;
			slot.queueBlockFrames = 0;
			slot.snapshotFrames = 0;
			break;
		}
	}
}




void AudioEngine::renameTrackInput(quint64 id, const QString& name)
{
	if (m_audioDev) { m_audioDev->renameTrackInputPort(id, name); }
}




auto AudioEngine::hasTrackInput(quint64 id) const -> bool
{
	if (id == 0) { return false; }
	const auto lock = std::lock_guard{m_trackInputMutex};
	return std::ranges::any_of(m_trackInputs, [id](const auto& slot) {
		return slot.id.load(std::memory_order_acquire) == id;
	});
}




auto AudioEngine::trackInputSource(quint64 id, const QString& displayName) const
	-> std::optional<AudioInputSource>
{
	if (!m_audioDev || !m_audioDev->supportsExternalPatchbayInput()) { return std::nullopt; }

	const auto lock = std::lock_guard{m_trackInputMutex};
	for (const auto& slot : m_trackInputs)
	{
		if (slot.id.load(std::memory_order_acquire) != id) { continue; }

		return makeAudioInputSource(
			QString::fromLatin1(TrackInputSourceId),
			displayName,
			slot.channels,
			{});
	}
	return std::nullopt;
}




namespace
{

template<typename Slot, typename Copy>
void enqueueTrackInputBlocks(Slot& slot, f_cnt_t frames, std::uint32_t blockCount, Copy&& copy)
{
	f_cnt_t frameOffset = 0;
	while (frameOffset < frames)
	{
		const auto write = slot.writeIndex.load(std::memory_order_relaxed);
		const auto next = (write + 1) % blockCount;
		if (next == slot.readIndex.load(std::memory_order_acquire))
		{
			slot.droppedFrames.fetch_add(frames - frameOffset, std::memory_order_relaxed);
			return;
		}

		const auto copied = std::min(frames - frameOffset, slot.queueBlockFrames);
		copy(write, frameOffset, copied);
		slot.blockFrames[write] = copied;
		slot.writeIndex.store(next, std::memory_order_release);
		frameOffset += copied;
	}
}

} // namespace

void AudioEngine::pushTrackInputFrames(quint64 id, PlanarBufferView<const float> frames)
{
	if (id == 0 || frames.empty() || frames.channels() == 0) { return; }

	const auto found = std::ranges::find_if(m_trackInputs, [id](const auto& candidate) {
		return candidate.id.load(std::memory_order_acquire) == id;
	});
	auto* slot = found == m_trackInputs.end() ? nullptr : &*found;
	if (!slot || !slot->queue || slot->queueBlockFrames == 0 || frames.channels() != slot->channels)
	{
		return;
	}

	enqueueTrackInputBlocks(*slot, frames.frames(), InputCaptureBlocks,
		[&](std::uint32_t write, f_cnt_t frameOffset, f_cnt_t copied) {
			for (ch_cnt_t channel = 0; channel < slot->channels; ++channel)
			{
				std::copy_n(frames.bufferPtr(channel) + frameOffset, copied,
					slot->queue->buffer(channel).data() + write * slot->queueBlockFrames);
			}
		});
}

void AudioEngine::pushTrackInputFrames(quint64 id, InterleavedBufferView<const float> frames)
{
	if (id == 0 || frames.empty() || frames.channels() == 0) { return; }

	const auto found = std::ranges::find_if(m_trackInputs, [id](const auto& slot) {
		return slot.id.load(std::memory_order_acquire) == id;
	});
	if (found == m_trackInputs.end() || !found->queue || found->queueBlockFrames == 0) { return; }

	enqueueTrackInputBlocks(*found, frames.frames(), InputCaptureBlocks,
		[&](std::uint32_t write, f_cnt_t frameOffset, f_cnt_t copied) {
			for (ch_cnt_t channel = 0; channel < found->channels; ++channel)
			{
				const auto inputChannel = std::min<ch_cnt_t>(channel, frames.channels() - 1);
				auto* destination = found->queue->buffer(channel).data() + write * found->queueBlockFrames;
				for (f_cnt_t frame = 0; frame < copied; ++frame)
				{
					destination[frame] = frames.framePtr(frameOffset + frame)[inputChannel];
				}
			}
		});
}




auto AudioEngine::trackInputBuffer(quint64 id) const -> PlanarBufferView<const float>
{
	if (id == 0) { return {}; }
	const auto lock = std::lock_guard{m_trackInputMutex};
	for (const auto& slot : m_trackInputs)
	{
		if (slot.id.load(std::memory_order_acquire) != id || !slot.snapshot) { continue; }
		return {slot.snapshot->allBuffers().data(), slot.channels, slot.snapshotFrames};
	}
	return {};
}

auto AudioEngine::trackInputDroppedFrames(quint64 id) const -> std::uint64_t
{
	const auto lock = std::lock_guard{m_trackInputMutex};
	const auto found = std::ranges::find_if(m_trackInputs, [id](const auto& slot) {
		return slot.id.load(std::memory_order_acquire) == id;
	});
	return found == m_trackInputs.end() ? 0 : found->droppedFrames.load(std::memory_order_relaxed);
}

void AudioEngine::setTrackInputCaptureFailed(quint64 id, bool failed)
{
	if (id == 0) { return; }
	auto changed = false;
	{
		const auto lock = std::lock_guard{m_trackInputMutex};
		for (auto& slot : m_trackInputs)
		{
			if (slot.id.load(std::memory_order_acquire) != id) { continue; }
			changed = slot.captureFailed.exchange(failed, std::memory_order_relaxed) != failed;
			break;
		}
	}
	if (changed)
	{
		QMetaObject::invokeMethod(this, [this] { emit inputSourcesChanged(); }, Qt::QueuedConnection);
	}
}

auto AudioEngine::trackInputCaptureFailed(quint64 id) const -> bool
{
	if (id == 0) { return false; }
	const auto lock = std::lock_guard{m_trackInputMutex};
	const auto found = std::ranges::find_if(m_trackInputs, [id](const auto& slot) {
		return slot.id.load(std::memory_order_acquire) == id;
	});
	return found != m_trackInputs.end() && found->captureFailed.load(std::memory_order_relaxed);
}




void AudioEngine::drainTrackInputFrames()
{
	const auto lock = std::lock_guard{m_trackInputMutex};
	for (auto& slot : m_trackInputs)
	{
		slot.snapshotFrames = 0;
		if (slot.id.load(std::memory_order_acquire) == 0 || !slot.queue || !slot.snapshot)
		{
			continue;
		}

		auto read = slot.readIndex.load(std::memory_order_relaxed);
		const auto write = slot.writeIndex.load(std::memory_order_acquire);
		f_cnt_t outputFrames = 0;
		const auto capacity = slot.queueBlockFrames * InputCaptureBlocks;

		while (read != write && outputFrames < capacity)
		{
			const auto frames = slot.blockFrames[read];
			const auto copied = std::min(frames, capacity - outputFrames);
			for (ch_cnt_t channel = 0; channel < slot.channels; ++channel)
			{
				const auto* source = slot.queue->buffer(channel).data() + read * slot.queueBlockFrames;
				auto* destination = slot.snapshot->buffer(channel).data() + outputFrames;
				std::copy_n(source, copied, destination);
			}
			outputFrames += copied;
			read = (read + 1) % static_cast<std::uint32_t>(InputCaptureBlocks);
		}
		slot.readIndex.store(read, std::memory_order_release);
		slot.snapshotFrames = outputFrames;
	}
}




void AudioEngine::renderStageNoteSetup()
{
	AudioEngineProfiler::Probe profilerProbe(m_profiler, AudioEngineProfiler::DetailType::NoteSetup);

	drainTrackInputFrames();

	if( m_clearSignal )
	{
		m_clearSignal = false;
		clearInternal();
	}

	// remove all play-handles that have to be deleted and delete
	// them if they still exist...
	// maybe this algorithm could be optimized...
	ConstPlayHandleList::Iterator it_rem = m_playHandlesToRemove.begin();
	while( it_rem != m_playHandlesToRemove.end() )
	{
		PlayHandleList::Iterator it = std::find( m_playHandles.begin(), m_playHandles.end(), *it_rem );

		if( it != m_playHandles.end() )
		{
			if (auto bus = (*it)->audioBusHandle()) { bus->removePlayHandle(*it); }
			if((*it)->type() == PlayHandle::Type::NotePlayHandle)
			{
				NotePlayHandleManager::release((NotePlayHandle*)*it);
			}
			else delete *it;
			m_playHandles.erase(it);
		}

		it_rem = m_playHandlesToRemove.erase( it_rem );
	}

	swapBuffers();

	// prepare master mix (clear internal buffers etc.)
	Mixer * mixer = Engine::mixer();
	mixer->prepareMasterMix();

	// create play-handles for new notes, samples etc.
	Engine::getSong()->processNextBuffer();

	// add all play-handles that have to be added
	for( LocklessListElement * e = m_newPlayHandles.popList(); e; )
	{
		m_playHandles += e->value;
		LocklessListElement * next = e->next;
		m_newPlayHandles.free( e );
		e = next;
	}
}



void AudioEngine::renderStageInstruments()
{
	AudioEngineProfiler::Probe profilerProbe(m_profiler, AudioEngineProfiler::DetailType::Instruments);

	AudioEngineWorkerThread::fillJobQueue(m_playHandles);
	AudioEngineWorkerThread::startAndWaitForJobs();
}



void AudioEngine::renderStageEffects()
{
	AudioEngineProfiler::Probe profilerProbe(m_profiler, AudioEngineProfiler::DetailType::Effects);

	// STAGE 2: process effects of all instrument- and sampletracks
	AudioEngineWorkerThread::fillJobQueue(m_audioBusHandles);
	AudioEngineWorkerThread::startAndWaitForJobs();

	// removed all play handles which are done
	for( PlayHandleList::Iterator it = m_playHandles.begin();
						it != m_playHandles.end(); )
	{
		if( ( *it )->affinityMatters() &&
			( *it )->affinity() != QThread::currentThread() )
		{
			++it;
			continue;
		}
		if( ( *it )->isFinished() )
		{
			if (auto bus = (*it)->audioBusHandle()) { bus->removePlayHandle(*it); }
			if((*it)->type() == PlayHandle::Type::NotePlayHandle)
			{
				NotePlayHandleManager::release((NotePlayHandle*)*it);
			}
			else delete *it;
			it = m_playHandles.erase(it);
		}
		else
		{
			++it;
		}
	}
}



void AudioEngine::renderStageMix()
{
	AudioEngineProfiler::Probe profilerProbe(m_profiler, AudioEngineProfiler::DetailType::Mixing);

	Mixer *mixer = Engine::mixer();
	mixer->masterMix(m_outputBufferWrite.get());

	MixHelpers::multiply(m_outputBufferWrite.get(), m_masterGain, m_framesPerPeriod);

	emit nextAudioBuffer(m_outputBufferRead.get());

	// and trigger LFOs
	EnvelopeAndLfoParameters::instances()->trigger();
	Controller::triggerFrameCounter();
	AutomatableModel::incrementPeriodCounter();
}



std::span<const SampleFrame> AudioEngine::renderNextPeriod()
{
	const auto lock = std::lock_guard{m_changeMutex};

	m_profiler.startPeriod();
	s_renderingThread = true;

	renderStageNoteSetup();     // STAGE 0: clear old play handles and buffers, setup new play handles
	renderStageInstruments();   // STAGE 1: run and render all play handles
	renderStageEffects();       // STAGE 2: process effects of all instrument- and sampletracks
	renderStageMix();           // STAGE 3: do master mix in mixer

	s_renderingThread = false;
	m_profiler.finishPeriod(outputSampleRate(), m_framesPerPeriod);
	m_outputBufferReadIndex = 0;

	return {m_outputBufferRead.get(), m_framesPerPeriod};
}

void AudioEngine::swapBuffers()
{
	std::swap(m_outputBufferRead, m_outputBufferWrite);
	zeroSampleFrames(m_outputBufferWrite.get(), m_framesPerPeriod);
}

void AudioEngine::clear()
{
	m_clearSignal = true;
}




void AudioEngine::clearNewPlayHandles()
{
	requestChangeInModel();
	for( LocklessListElement * e = m_newPlayHandles.popList(); e; )
	{
		LocklessListElement * next = e->next;
		m_newPlayHandles.free( e );
		e = next;
	}
	doneChangeInModel();
}



// removes all play-handles. this is necessary, when the song is stopped ->
// all remaining notes etc. would be played until their end
void AudioEngine::clearInternal()
{
	// TODO: m_midiClient->noteOffAll();
	for (auto ph : m_playHandles)
	{
		if (ph->type() != PlayHandle::Type::InstrumentPlayHandle)
		{
			m_playHandlesToRemove.push_back(ph);
		}
	}
}

void AudioEngine::doSetAudioDevice( AudioDevice * _dev )
{
	// TODO: Use shared_ptr here in the future.
	// Currently, this is safe, because this is only called by
	// ProjectRenderer, and after ProjectRenderer calls this function,
	// it does not access the old device anymore.
	if( m_audioDev != m_oldAudioDev ) {delete m_audioDev;}

	if( _dev )
	{
		m_audioDev = _dev;
	}
	else
	{
		printf( "param _dev == NULL in AudioEngine::setAudioDevice(...). "
					"Trying any working audio-device\n" );
		m_audioDev = tryAudioDevices();
	}

	emit inputSourcesChanged();
}

void AudioEngine::setAudioDevice(AudioDevice* _dev, bool startNow)
{
	stopProcessing();

	doSetAudioDevice( _dev );

	emit qualitySettingsChanged();
	emit sampleRateChanged();

	if (startNow) { startProcessing(); }
}

void AudioEngine::notifyAudioDeviceChanged(
	bool input, const QString& previousDevice, const QString& currentDevice, bool available)
{
	emit audioDeviceChanged(input, previousDevice, currentDevice, available);
}




void AudioEngine::storeAudioDevice()
{
	if( !m_oldAudioDev )
	{
		m_oldAudioDev = m_audioDev;
	}
}




void AudioEngine::restoreAudioDevice()
{
	if( m_oldAudioDev && m_audioDev != m_oldAudioDev )
	{
		stopProcessing();
		delete m_audioDev;

		m_audioDev = m_oldAudioDev;
		emit inputSourcesChanged();
		emit sampleRateChanged();

		startProcessing();
	}
	m_oldAudioDev = nullptr;
}




void AudioEngine::removeAudioBusHandle(AudioBusHandle* busHandle)
{
	requestChangeInModel();

	auto it = std::find(m_audioBusHandles.begin(), m_audioBusHandles.end(), busHandle);
	if (it != m_audioBusHandles.end())
	{
		m_audioBusHandles.erase(it);
	}
	doneChangeInModel();
}


bool AudioEngine::addPlayHandle( PlayHandle* handle )
{
	if (handle->type() == PlayHandle::Type::SampleRecordPlayHandle)
	{
		requestChangeInModel();
		m_playHandles += handle;
		if (auto bus = handle->audioBusHandle()) { bus->addPlayHandle(handle); }
		doneChangeInModel();
		return true;
	}

	// Only add play handles if we have the CPU capacity to process them.
	// Instrument play handles are not added during playback, but when the
	// associated instrument is created, so add those unconditionally.
	if (handle->type() == PlayHandle::Type::InstrumentPlayHandle ||
		handle->type() == PlayHandle::Type::SampleRecordPlayHandle || !criticalXRuns())
	{
		m_newPlayHandles.push( handle );
		if (auto bus = handle->audioBusHandle()) { bus->addPlayHandle(handle); }
		return true;
	}

	if( handle->type() == PlayHandle::Type::NotePlayHandle )
	{
		NotePlayHandleManager::release( (NotePlayHandle*)handle );
	}
	else delete handle;

	return false;
}


void AudioEngine::removePlayHandle(PlayHandle * ph)
{
	requestChangeInModel();
	// check thread affinity as we must not delete play-handles
	// which were created in a thread different than the audio engine thread
	if (ph->affinityMatters() && ph->affinity() == QThread::currentThread())
	{
		if (auto bus = ph->audioBusHandle()) { bus->removePlayHandle(ph); }
		bool removedFromList = false;
		// Check m_newPlayHandles first because doing it the other way around
		// creates a race condition
		for( LocklessListElement * e = m_newPlayHandles.first(),
				* ePrev = nullptr; e; ePrev = e, e = e->next )
		{
			if (e->value == ph)
			{
				if( ePrev )
				{
					ePrev->next = e->next;
				}
				else
				{
					m_newPlayHandles.setFirst( e->next );
				}
				m_newPlayHandles.free( e );
				removedFromList = true;
				break;
			}
		}
		// Now check m_playHandles
		PlayHandleList::Iterator it = std::find(m_playHandles.begin(), m_playHandles.end(), ph);
		if (it != m_playHandles.end())
		{
			m_playHandles.erase(it);
			removedFromList = true;
		}
		// Only deleting PlayHandles that were actually found in the list
		// "fixes crash when previewing a preset under high load"
		// (See tobydox's 2008 commit 4583e48)
		if ( removedFromList )
		{
			if (ph->type() == PlayHandle::Type::NotePlayHandle)
			{
				NotePlayHandleManager::release(dynamic_cast<NotePlayHandle*>(ph));
			}
			else { delete ph; }
		}
	}
	else
	{
		m_playHandlesToRemove.push_back(ph);
	}
	doneChangeInModel();
}




void AudioEngine::removePlayHandlesOfTypes(Track * track, PlayHandle::Types types)
{
	requestChangeInModel();
	PlayHandleList::Iterator it = m_playHandles.begin();
	while( it != m_playHandles.end() )
	{
		if ((*it)->isFromTrack(track) && ((*it)->type() & types))
		{
			if (auto bus = (*it)->audioBusHandle()) { bus->removePlayHandle(*it); }
			if((*it)->type() == PlayHandle::Type::NotePlayHandle)
			{
				NotePlayHandleManager::release((NotePlayHandle*)*it);
			}
			else delete *it;
			it = m_playHandles.erase(it);
		}
		else
		{
			++it;
		}
	}
	doneChangeInModel();
}




void AudioEngine::requestChangeInModel()
{
	if (s_renderingThread) { return; }
	m_changeMutex.lock();
}

auto AudioEngine::tryRequestChangeInModel() -> bool
{
	return s_renderingThread || m_changeMutex.try_lock();
}

void AudioEngine::doneChangeInModel()
{
	if (s_renderingThread) { return; }
	m_changeMutex.unlock();
}

bool AudioEngine::isAudioDevNameValid(QString name)
{
#ifdef MXM_HAVE_SDL
	if (name == AudioSdl::name())
	{
		return true;
	}
#endif


#ifdef MXM_HAVE_ALSA
	if (name == AudioAlsa::name())
	{
		return true;
	}
#endif


#ifdef MXM_HAVE_PULSEAUDIO
	if (name == AudioPulseAudio::name())
	{
		return true;
	}
#endif

#ifdef MXM_HAVE_SNDIO
	if (name == AudioSndio::name())
	{
		return true;
	}
#endif

#ifdef MXM_HAVE_JACK
	if (name == AudioJack::name())
	{
		return true;
	}
#endif


#ifdef MXM_HAVE_PORTAUDIO
	if (name == AudioPortAudio::name())
	{
		return true;
	}
#endif


	if (name == AudioDummy::name())
	{
		return true;
	}

	return false;
}

bool AudioEngine::isMidiDevNameValid(QString name)
{
#ifdef MXM_HAVE_ALSA
	if (name == MidiAlsaSeq::name() || name == MidiAlsaRaw::name())
	{
		return true;
	}
#endif

#ifdef MXM_HAVE_JACK
	if (name == MidiJack::name())
	{
		return true;
	}
#endif

#ifdef MXM_HAVE_SNDIO
	if (name == MidiSndio::name())
	{
		return true;
	}
#endif

#ifdef MXM_HAVE_WINMM
	if (name == MidiWinMM::name())
	{
		return true;
	}
#endif

#ifdef MXM_BUILD_APPLE
    if (name == MidiApple::name())
    {
		return true;
    }
#endif

    if (name == MidiDummy::name())
    {
		return true;
    }

	return false;
}

AudioDevice * AudioEngine::tryAudioDevices()
{
	bool success_ful = false;
	AudioDevice * dev = nullptr;
	QString dev_name = ConfigManager::inst()->value( "audioengine", "audiodev" );
	if( !isAudioDevNameValid( dev_name ) )
	{
		dev_name = "";
	}

	m_audioDevStartFailed = false;

#ifdef MXM_HAVE_SDL
	if( dev_name == AudioSdl::name() || dev_name == "" )
	{
		dev = new AudioSdl( success_ful, this );
		if( success_ful )
		{
			m_audioDevName = AudioSdl::name();
			return dev;
		}
		delete dev;
	}
#endif


#ifdef MXM_HAVE_ALSA
	if( dev_name == AudioAlsa::name() || dev_name == "" )
	{
		dev = new AudioAlsa( success_ful, this );
		if( success_ful )
		{
			m_audioDevName = AudioAlsa::name();
			return dev;
		}
		delete dev;
	}
#endif


#ifdef MXM_HAVE_PULSEAUDIO
	if( dev_name == AudioPulseAudio::name() || dev_name == "" )
	{
		dev = new AudioPulseAudio( success_ful, this );
		if( success_ful )
		{
			m_audioDevName = AudioPulseAudio::name();
			return dev;
		}
		delete dev;
	}
#endif

#ifdef MXM_HAVE_SNDIO
	if( dev_name == AudioSndio::name() || dev_name == "" )
	{
		dev = new AudioSndio( success_ful, this );
		if( success_ful )
		{
			m_audioDevName = AudioSndio::name();
			return dev;
		}
		delete dev;
	}
#endif


#ifdef MXM_HAVE_JACK
	if( dev_name == AudioJack::name() || dev_name == "" )
	{
		dev = new AudioJack( success_ful, this );
		if( success_ful )
		{
			m_audioDevName = AudioJack::name();
			return dev;
		}
		delete dev;
	}
#endif


#ifdef MXM_HAVE_PORTAUDIO
	if (dev_name == AudioPortAudio::name() || dev_name.isEmpty())
	{
		dev = new AudioPortAudio( success_ful, this );
		if( success_ful )
		{
			m_audioDevName = AudioPortAudio::name();
			return dev;
		}
		delete dev;
	}
#endif


	// add more device-classes here...
	//dev = new audioXXXX( SAMPLE_RATES[m_qualityLevel], success_ful, this );
	//if( sucess_ful )
	//{
	//	return dev;
	//}
	//delete dev

	if( dev_name != AudioDummy::name() )
	{
		printf( "No audio-driver working - falling back to dummy-audio-"
			"driver\nYou can render your songs and listen to the output "
			"files...\n" );

		m_audioDevStartFailed = true;
	}

	m_audioDevName = AudioDummy::name();

	return new AudioDummy( success_ful, this );
}




MidiClient * AudioEngine::tryMidiClients()
{
	QString client_name = ConfigManager::inst()->value( "audioengine", "mididev" );
	if( !isMidiDevNameValid( client_name ) )
	{
		client_name = "";
	}

#ifdef MXM_HAVE_ALSA
	if( client_name == MidiAlsaSeq::name() || client_name == "" )
	{
		auto malsas = new MidiAlsaSeq;
		if( malsas->isRunning() )
		{
			m_midiClientName = MidiAlsaSeq::name();
			return malsas;
		}
		delete malsas;
	}

	if( client_name == MidiAlsaRaw::name() || client_name == "" )
	{
		auto malsar = new MidiAlsaRaw;
		if( malsar->isRunning() )
		{
			m_midiClientName = MidiAlsaRaw::name();
			return malsar;
		}
		delete malsar;
	}
#endif

#ifdef MXM_HAVE_JACK
	if( client_name == MidiJack::name() || client_name == "" )
	{
		auto mjack = new MidiJack;
		if( mjack->isRunning() )
		{
			m_midiClientName = MidiJack::name();
			return mjack;
		}
		delete mjack;
	}
#endif

#ifdef MXM_HAVE_SNDIO
	if( client_name == MidiSndio::name() || client_name == "" )
	{
		MidiSndio * msndio = new MidiSndio;
		if( msndio->isRunning() )
		{
			m_midiClientName = MidiSndio::name();
			return msndio;
		}
		delete msndio;
	}
#endif

#ifdef MXM_HAVE_WINMM
	if( client_name == MidiWinMM::name() || client_name == "" )
	{
		MidiWinMM * mwmm = new MidiWinMM;
//		if( moss->isRunning() )
		{
			m_midiClientName = MidiWinMM::name();
			return mwmm;
		}
		delete mwmm;
	}
#endif

#ifdef MXM_BUILD_APPLE
    printf( "trying midi apple...\n" );
    if( client_name == MidiApple::name() || client_name == "" )
    {
        MidiApple * mapple = new MidiApple;
        m_midiClientName = MidiApple::name();
        printf( "Returning midi apple\n" );
        return mapple;
    }
    printf( "midi apple didn't work: client_name=%s\n", client_name.toUtf8().constData());
#endif

	if(client_name != MidiDummy::name())
	{
		if (client_name.isEmpty())
		{
			printf("Unknown MIDI-client. ");
		}
		else
		{
			printf("Couldn't create %s MIDI-client. ", client_name.toUtf8().constData());
		}
		printf("Will use dummy-MIDI-client.\n");
	}

	m_midiClientName = MidiDummy::name();

	return new MidiDummy;
}

} // namespace mxm
