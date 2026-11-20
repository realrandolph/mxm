/*
 * Sample.h
 *
 * Copyright (c) 2025 saker <sakertooth@gmail.com>
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

#ifndef MXM_SAMPLE_H
#define MXM_SAMPLE_H

#include <memory>
#include <span>
#include <vector>

#include "AudioBufferView.h"
#include "AudioChannelLayout.h"
#include "AudioResampler.h"
#include "Note.h"
#include "SampleBuffer.h"
#include "mxm_export.h"

namespace mxm {
class MXM_EXPORT Sample
{
public:
	enum class Loop
	{
		Off,
		On,
		PingPong
	};

	class MXM_EXPORT PlaybackState
	{
	public:
		PlaybackState(AudioResampler::Mode interpolationMode = AudioResampler::Mode::Linear, int frameIndex = 0)
			: PlaybackState(interpolationMode, DEFAULT_CHANNELS, frameIndex)
		{
		}

		PlaybackState(AudioResampler::Mode interpolationMode, ch_cnt_t channels, int frameIndex = 0)
			: m_resampler(interpolationMode, std::max<ch_cnt_t>(channels, 1))
			, m_channels(std::max<ch_cnt_t>(channels, 1))
			, m_buffer(static_cast<size_t>(m_channels) * DEFAULT_BUFFER_SIZE, 0.0f)
			, m_resampledBuffer(static_cast<size_t>(m_channels) * DEFAULT_BUFFER_SIZE, 0.0f)
			, m_frameIndex(frameIndex)
		{
		}

		PlaybackState(PlaybackState&& other) noexcept
			: m_resampler(std::move(other.m_resampler))
			, m_channels(other.m_channels)
			, m_buffer(std::move(other.m_buffer))
			, m_resampledBuffer(std::move(other.m_resampledBuffer))
			, m_frameIndex(other.m_frameIndex)
			, m_backwards(other.m_backwards)
		{
			m_bufferView = {};
			other.m_bufferView = {};
		}

		PlaybackState& operator=(PlaybackState&& other) noexcept
		{
			if (this != &other)
			{
				m_resampler = std::move(other.m_resampler);
				m_channels = other.m_channels;
				m_buffer = std::move(other.m_buffer);
				m_resampledBuffer = std::move(other.m_resampledBuffer);
				m_frameIndex = other.m_frameIndex;
				m_backwards = other.m_backwards;
				m_bufferView = {};
				other.m_bufferView = {};
			}
			return *this;
		}

		auto frameIndex() const -> int { return m_frameIndex; }
		auto backwards() const -> bool { return m_backwards; }
		auto channels() const -> ch_cnt_t { return m_channels; }

		void setFrameIndex(int index) { m_frameIndex = index; }
		void setBackwards(bool backwards) { m_backwards = backwards; }
		void setChannels(ch_cnt_t channels)
		{
			const auto validChannels = std::max<ch_cnt_t>(channels, 1);
			if (m_channels == validChannels && !m_buffer.empty()) { return; }
			m_channels = validChannels;
			m_resampler = AudioResampler(m_resampler.mode(), m_channels);
			m_buffer.assign(static_cast<size_t>(m_channels) * DEFAULT_BUFFER_SIZE, 0.0f);
			m_resampledBuffer.assign(static_cast<size_t>(m_channels) * DEFAULT_BUFFER_SIZE, 0.0f);
			m_bufferView = {};
		}

	private:
		AudioResampler m_resampler;
		ch_cnt_t m_channels = DEFAULT_CHANNELS;
		std::vector<float> m_buffer;
		std::span<float> m_bufferView;
		std::vector<float> m_resampledBuffer;
		int m_frameIndex = 0;
		bool m_backwards = false;
		friend class Sample;
	};

	Sample() = default;

	Sample(const SampleFrame* data, size_t numFrames, int sampleRate = Engine::audioEngine()->outputSampleRate());
	Sample(const Sample& other);
	Sample(Sample&& other) noexcept;
	explicit Sample(std::shared_ptr<const SampleBuffer> buffer);

	auto operator=(const Sample&) -> Sample&;
	auto operator=(Sample&&) noexcept -> Sample&;

	auto play(SampleFrame* dst, PlaybackState* state, size_t numFrames, Loop loopMode = Loop::Off,
		double ratio = 1.0) const -> bool;
	auto play(PlanarBufferView<float> dst, const AudioChannelLayout& dstLayout,
		PlaybackState* state, size_t numFrames, Loop loopMode = Loop::Off,
		double ratio = 1.0) const -> bool;

	auto sampleDuration() const -> std::chrono::milliseconds;
	auto sampleFile() const -> const QString& { return m_buffer->audioFile(); }
	auto sampleRate() const -> int { return m_buffer->sampleRate(); }
	auto sampleSize() const -> size_t { return m_buffer->size(); }

	auto toBase64() const -> QString { return m_buffer->toBase64(); }

	auto data() const -> const SampleFrame* { return m_buffer->data(); }
	auto buffer() const -> std::shared_ptr<const SampleBuffer> { return m_buffer; }
	auto startFrame() const -> int { return m_startFrame.load(std::memory_order_relaxed); }
	auto endFrame() const -> int { return m_endFrame.load(std::memory_order_relaxed); }
	auto loopStartFrame() const -> int { return m_loopStartFrame.load(std::memory_order_relaxed); }
	auto loopEndFrame() const -> int { return m_loopEndFrame.load(std::memory_order_relaxed); }
	auto amplification() const -> float { return m_amplification.load(std::memory_order_relaxed); }
	auto frequency() const -> float { return m_frequency.load(std::memory_order_relaxed); }
	auto reversed() const -> bool { return m_reversed.load(std::memory_order_relaxed); }

	void setStartFrame(int startFrame) { m_startFrame.store(startFrame, std::memory_order_relaxed); }
	void setEndFrame(int endFrame) { m_endFrame.store(endFrame, std::memory_order_relaxed); }
	void setLoopStartFrame(int loopStartFrame) { m_loopStartFrame.store(loopStartFrame, std::memory_order_relaxed); }
	void setLoopEndFrame(int loopEndFrame) { m_loopEndFrame.store(loopEndFrame, std::memory_order_relaxed); }
	void setAllPointFrames(int startFrame, int endFrame, int loopStartFrame, int loopEndFrame);
	void setAmplification(float amplification) { m_amplification.store(amplification, std::memory_order_relaxed); }
	void setFrequency(float frequency) { m_frequency.store(frequency, std::memory_order_relaxed); }
	void setReversed(bool reversed) { m_reversed.store(reversed, std::memory_order_relaxed); }

private:
	f_cnt_t render(SampleFrame* dst, f_cnt_t size, PlaybackState* state, Loop loop) const;
	f_cnt_t renderNative(float* dst, f_cnt_t size, PlaybackState* state, Loop loop) const;
	std::shared_ptr<const SampleBuffer> m_buffer = SampleBuffer::emptyBuffer();
	std::atomic<int> m_startFrame = 0;
	std::atomic<int> m_endFrame = 0;
	std::atomic<int> m_loopStartFrame = 0;
	std::atomic<int> m_loopEndFrame = 0;
	std::atomic<float> m_amplification = 1.0f;
	std::atomic<float> m_frequency = DefaultBaseFreq;
	std::atomic<bool> m_reversed = false;
};
} // namespace mxm
#endif
