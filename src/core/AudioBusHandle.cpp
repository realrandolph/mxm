/*
 * AudioBusHandle.cpp - ThreadableJob between PlayHandle and MixerChannel
 *
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 * Copyright (c) 2025 Johannes Lorenz <jlsf2013$users.sourceforge.net, $=@>
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

#include "AudioBusHandle.h"

#include <QMutexLocker>

#include "AudioChannelBuffer.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "EffectChain.h"
#include "Mixer.h"
#include "Engine.h"
#include "MixHelpers.h"

namespace mxm
{

AudioBusHandle::AudioBusHandle(const QString& name, bool hasEffectChain,
	FloatModel* volumeModel, FloatModel* panningModel,
	BoolModel* mutedModel) :
	m_bufferUsage(false),
	m_channelLayout(AudioChannelLayout::stereo()),
	m_buffer(nullptr),
	m_extOutputEnabled(false),
	m_nextMixerChannel(0),
	m_name(name),
	m_effects(hasEffectChain ? new EffectChain(nullptr) : nullptr),
	m_volumeModel(volumeModel),
	m_panningModel(panningModel),
	m_mutedModel(mutedModel)
{
	m_buffer = createChannelBuffer(Engine::audioEngine()->framesPerPeriod(), m_channelLayout);

	Engine::audioEngine()->addAudioBusHandle(this);
	setExtOutputEnabled(true);
}




AudioBusHandle::~AudioBusHandle()
{
	setExtOutputEnabled(false);
	Engine::audioEngine()->removeAudioBusHandle(this);
}




void AudioBusHandle::setChannelLayout(const AudioChannelLayout& layout)
{
	if (layout.channels() == 0 || layout == m_channelLayout) { return; }

	const auto guard = Engine::audioEngine()->requestChangesGuard();
	m_channelLayout = layout;
	m_buffer = createChannelBuffer(Engine::audioEngine()->framesPerPeriod(), m_channelLayout);

	// Recreate hardware ports so every logical channel gets its own port.
	if (m_extOutputEnabled && Engine::audioEngine()->audioDev())
	{
		Engine::audioEngine()->audioDev()->registerPort(this);
	}
}




void AudioBusHandle::setExtOutputEnabled(bool enabled)
{
	if (enabled != m_extOutputEnabled)
	{
		m_extOutputEnabled = enabled;
		if (m_extOutputEnabled)
		{
			Engine::audioEngine()->audioDev()->registerPort(this);
		}
		else
		{
			Engine::audioEngine()->audioDev()->unregisterPort(this);
		}
	}
}




void AudioBusHandle::setName(const QString& newName)
{
	m_name = newName;
	Engine::audioEngine()->audioDev()->renamePort(this);
}




bool AudioBusHandle::processEffects()
{
	if (m_effects)
	{
		bool more = m_effects->processAudioBuffer(*m_buffer);
		return more;
	}
	return false;
}




void AudioBusHandle::applyVolumeAndPan()
{
	// Panners are stereo controls in the existing model contract. They affect
	// the front left/right pair of a surround bus and leave centre, LFE and
	// surround channels at the bus volume. Duplicating the mono channel
	// before effects lets stereo-only effects process mono tracks usefully.
	auto logical = logicalBuffers(*m_buffer, m_channelLayout);
	const auto logicalChannels = logical.channels();
	if (logicalChannels == 0) { return; }

	// Mono is processed as a duplicated stereo pair so stereo-only effects
	// still receive both sides. The pair is folded back afterwards.
	const auto channels = m_channelLayout.channels() == 1
		? static_cast<ch_cnt_t>(DEFAULT_CHANNELS) : logicalChannels;

	if (m_channelLayout.channels() == 1)
	{
		std::copy_n(logical.bufferPtr(0), m_buffer->frames(), m_buffer->buffer(1).data());
	}

	ValueBuffer* volBuf = m_volumeModel ? m_volumeModel->valueBuffer() : nullptr;
	ValueBuffer* panBuf = m_panningModel ? m_panningModel->valueBuffer() : nullptr;

	const auto staticVolume = m_volumeModel ? m_volumeModel->value() * 0.01f : 1.0f;
	const auto staticPan = m_panningModel ? m_panningModel->value() * 0.01f : 0.0f;

	for (f_cnt_t frame = 0; frame < m_buffer->frames(); ++frame)
	{
		const auto volume = staticVolume * (volBuf ? volBuf->values()[frame] * 0.01f : 1.0f);
		const auto pan = panBuf ? panBuf->values()[frame] * 0.01f : staticPan;
		const auto left = pan <= 0.f ? 1.f : 1.f - pan;
		const auto right = pan >= 0.f ? 1.f : 1.f + pan;

		for (ch_cnt_t channel = 0; channel < channels; ++channel)
		{
			auto gain = volume;
			const auto role = m_channelLayout.role(channel);
			if (m_channelLayout.channels() > 1)
			{
				if (role == SpeakerRole::FrontLeft || (role == SpeakerRole::Unknown && channel == 0))
				{
					gain *= left;
				}
				else if (role == SpeakerRole::FrontRight || (role == SpeakerRole::Unknown && channel == 1))
				{
					gain *= right;
				}
			}
			logical.bufferPtr(channel)[frame] *= gain;
		}
	}
}




void AudioBusHandle::doProcessing()
{
	if (m_mutedModel && m_mutedModel->value())
	{
		return;
	}

	const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();

	// clear the buffer
	m_buffer->silenceAllChannels();

	auto logical = logicalBuffers(*m_buffer, m_channelLayout);

	// Mix all play handles into this bus. Native multichannel play handles are
	// converted by speaker role here; stereo play handles feed the front pair.
	for (PlayHandle* ph : m_playHandles)
	{
		if (auto* multichannel = ph->multichannelBuffer())
		{
			if (multichannel->hasAnySignal())
			{
				m_bufferUsage = true;
				MixHelpers::addPlanarConverted(
					logicalBuffers(*multichannel, ph->multichannelLayout()),
					ph->multichannelLayout(), logical, m_channelLayout);
			}
			ph->releaseBuffer();
			continue;
		}

		if (ph->buffer())
		{
			if (ph->usesBuffer()
				&& (ph->type() == PlayHandle::Type::NotePlayHandle
					|| !MixHelpers::isSilent(ph->buffer(), fpp)))
			{
				m_bufferUsage = true;

				const auto findRole = [this](SpeakerRole wanted, ch_cnt_t fallback) -> ch_cnt_t {
					for (ch_cnt_t channel = 0; channel < m_channelLayout.channels(); ++channel)
					{
						if (m_channelLayout.role(channel) == wanted) { return channel; }
					}
					return std::min(fallback, static_cast<ch_cnt_t>(m_channelLayout.channels() - 1));
				};

				const auto leftChannel = findRole(SpeakerRole::FrontLeft, 0);
				const auto rightChannel = m_channelLayout.channels() > 1
					? findRole(SpeakerRole::FrontRight, 1) : leftChannel;

				for (f_cnt_t frame = 0; frame < fpp; ++frame)
				{
					const auto& sample = ph->buffer()[frame];
					if (m_channelLayout.channels() == 1)
					{
						logical.bufferPtr(0)[frame] += sample.average();
					}
					else
					{
						logical.bufferPtr(leftChannel)[frame] += sample.left();
						if (rightChannel != leftChannel)
						{
							logical.bufferPtr(rightChannel)[frame] += sample.right();
						}
						else
						{
							logical.bufferPtr(leftChannel)[frame] += sample.right();
						}
					}
				}
			}
			ph->releaseBuffer(); 	// gets rid of playhandle's buffer and sets
									// pointer to null, so if it doesn't get re-acquired we know to skip it next time
		}
	}

	if (m_bufferUsage)
	{
		applyVolumeAndPan();

		const auto sanitized = Engine::audioEngine()->sanitizationEnabled()
			? m_buffer->sanitizeAll() : false;
		m_corrupted.store(sanitized, std::memory_order_relaxed);

		// Update silence status of all channels for this bus output
		m_buffer->updateAllSilenceFlags();
	}

	// Effects are stereo processors. Keep the scratch interleaved buffer in
	// sync with the front pair; remaining surround channels pass through
	// untouched instead of being discarded. This also feeds an all-silent
	// period so stereo effect tails can finish correctly.
	toInterleaved(m_buffer->groupBuffers(0), m_buffer->interleavedBuffer());
	const bool anyOutputAfterEffects = processEffects();

	if (m_bufferUsage || anyOutputAfterEffects)
	{
		// Collapse the duplicated mono channel after stereo effects.
		if (m_channelLayout.channels() == 1)
		{
			auto* mono = logical.bufferPtr(0);
			const auto* side = m_buffer->buffer(1).data();
			for (f_cnt_t frame = 0; frame < fpp; ++frame)
			{
				mono[frame] = 0.5f * (mono[frame] + side[frame]);
			}
			m_buffer->silenceChannels(0b10, 2);
		}

		m_buffer->updateAllSilenceFlags();
	}

	if (anyOutputAfterEffects || m_bufferUsage)
	{
		// TODO: improve the flow here - convert to pull model
		Engine::mixer()->mixToChannel(*m_buffer, m_channelLayout, m_nextMixerChannel);
		m_bufferUsage = false;
	}
}




void AudioBusHandle::addPlayHandle(PlayHandle* handle)
{
	QMutexLocker lockGuard(&m_playHandleLock);
	m_playHandles.append(handle);
}


void AudioBusHandle::removePlayHandle(PlayHandle* handle)
{
	QMutexLocker lockGuard(&m_playHandleLock);
	PlayHandleList::Iterator it = std::find(m_playHandles.begin(), m_playHandles.end(), handle);
	if (it != m_playHandles.end())
	{
		m_playHandles.erase(it);
	}
}

} // namespace mxm
