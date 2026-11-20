/*
 * Sample.cpp
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

#include "Sample.h"

#include "AudioChannelLayout.h"

namespace mxm {

Sample::Sample(const SampleFrame* data, size_t numFrames, int sampleRate)
	: m_buffer(std::make_shared<SampleBuffer>(data, numFrames, sampleRate))
	, m_startFrame(0)
	, m_endFrame(m_buffer->size())
	, m_loopStartFrame(0)
	, m_loopEndFrame(m_buffer->size())
{
}

Sample::Sample(std::shared_ptr<const SampleBuffer> buffer)
	: m_buffer(buffer)
	, m_startFrame(0)
	, m_endFrame(m_buffer->size())
	, m_loopStartFrame(0)
	, m_loopEndFrame(m_buffer->size())
{
}

Sample::Sample(const Sample& other)
	: m_buffer(other.m_buffer)
	, m_startFrame(other.startFrame())
	, m_endFrame(other.endFrame())
	, m_loopStartFrame(other.loopStartFrame())
	, m_loopEndFrame(other.loopEndFrame())
	, m_amplification(other.amplification())
	, m_frequency(other.frequency())
	, m_reversed(other.reversed())
{
}

Sample::Sample(Sample&& other) noexcept
	: m_buffer(std::move(other.m_buffer))
	, m_startFrame(other.startFrame())
	, m_endFrame(other.endFrame())
	, m_loopStartFrame(other.loopStartFrame())
	, m_loopEndFrame(other.loopEndFrame())
	, m_amplification(other.amplification())
	, m_frequency(other.frequency())
	, m_reversed(other.reversed())
{
}

auto Sample::operator=(const Sample& other) -> Sample&
{
	m_buffer = other.m_buffer;
	m_startFrame = other.startFrame();
	m_endFrame = other.endFrame();
	m_loopStartFrame = other.loopStartFrame();
	m_loopEndFrame = other.loopEndFrame();
	m_amplification = other.amplification();
	m_frequency = other.frequency();
	m_reversed = other.reversed();

	return *this;
}

auto Sample::operator=(Sample&& other) noexcept -> Sample&
{
	m_buffer = std::move(other.m_buffer);
	m_startFrame = other.startFrame();
	m_endFrame = other.endFrame();
	m_loopStartFrame = other.loopStartFrame();
	m_loopEndFrame = other.loopEndFrame();
	m_amplification = other.amplification();
	m_frequency = other.frequency();
	m_reversed = other.reversed();

	return *this;
}

bool Sample::play(SampleFrame* dst, PlaybackState* state, size_t numFrames, Loop loop, double ratio) const
{
	if (!m_buffer || m_buffer->empty() || !state) { return false; }

	state->m_frameIndex = std::max<int>(m_startFrame, state->m_frameIndex);

	const auto sampleRateRatio = static_cast<double>(Engine::audioEngine()->outputSampleRate()) / m_buffer->sampleRate();
	const auto freqRatio = frequency() / DefaultBaseFreq;
	state->m_resampler.setRatio(sampleRateRatio * freqRatio * ratio);

	// TODO: These kind of playback pipelines/graphs are repeated within other parts of the codebase that work with
	// audio samples. We should find a way to unify this but the right abstraction is not so clear yet.
	while (numFrames > 0)
	{
		if (state->m_bufferView.empty())
		{
			const auto rendered = render(reinterpret_cast<SampleFrame*>(state->m_buffer.data()), DEFAULT_BUFFER_SIZE, state, loop);
			state->m_bufferView = {state->m_buffer.data(), rendered * state->m_channels};
		}
 
		const auto [inputFramesUsed, outputFramesGenerated] = state->m_resampler.process(
			{state->m_bufferView.data(), state->m_channels, static_cast<f_cnt_t>(state->m_bufferView.size() / state->m_channels)},
			{&dst[0][0], 2, static_cast<f_cnt_t>(numFrames)});

		if (inputFramesUsed == 0 && outputFramesGenerated == 0)
		{
			std::fill_n(dst, numFrames, SampleFrame{});
			break;
		}

		state->m_bufferView = state->m_bufferView.subspan(inputFramesUsed * state->m_channels);
		dst += outputFramesGenerated;
		numFrames -= outputFramesGenerated;
	}

	return numFrames < Engine::audioEngine()->framesPerPeriod();
}

bool Sample::play(PlanarBufferView<float> dst, const AudioChannelLayout& dstLayout,
	PlaybackState* state, size_t numFrames, Loop loopMode, double ratio) const
{
	if (!m_buffer || m_buffer->empty() || !state) { return false; }

	const auto nativeChannels = m_buffer->channels();
	if (state->m_channels != nativeChannels)
	{
		state->setChannels(nativeChannels);
	}

	state->m_frameIndex = std::max<int>(m_startFrame, state->m_frameIndex);

	const auto sampleRateRatio = static_cast<double>(Engine::audioEngine()->outputSampleRate()) / m_buffer->sampleRate();
	const auto freqRatio = frequency() / DefaultBaseFreq;
	state->m_resampler.setRatio(sampleRateRatio * freqRatio * ratio);

	std::array<float, MaxChannelsPerAudioBuffer> destinationFrame{};
	const auto convertFrame = [&](const float* srcFrame, f_cnt_t dstFrame) {
		std::fill_n(destinationFrame.begin(), dstLayout.channels(), 0.0f);
		addFrameConverted(srcFrame, m_buffer->layout(), destinationFrame.data(), dstLayout);
		for (ch_cnt_t ch = 0; ch < dstLayout.channels() && ch < dst.channels(); ++ch)
		{
			dst.sample(ch, dstFrame) += destinationFrame[ch];
		}
	};

	// libsamplerate introduces a frame of latency even at unity ratio. A
	// unity-ratio native read avoids that and keeps sample identity exact.
	if (state->m_resampler.ratio() == 1.0)
	{
		const auto rendered = renderNative(state->m_buffer.data(),
			static_cast<f_cnt_t>(std::min<size_t>(numFrames, DEFAULT_BUFFER_SIZE)), state, loopMode);
		for (f_cnt_t frame = 0; frame < rendered; ++frame)
		{
			if (frame < dst.frames())
			{
				convertFrame(state->m_buffer.data() + static_cast<size_t>(frame) * nativeChannels, frame);
			}
		}
		state->m_bufferView = {};
		return rendered < Engine::audioEngine()->framesPerPeriod();
	}

	size_t currentDstFrame = 0;
	size_t remainingFrames = numFrames;

	while (remainingFrames > 0)
	{
		if (state->m_bufferView.empty())
		{
			const auto rendered = renderNative(state->m_buffer.data(), DEFAULT_BUFFER_SIZE, state, loopMode);
			state->m_bufferView = {state->m_buffer.data(), rendered * nativeChannels};
		}

		const auto chunkFrames = static_cast<f_cnt_t>(std::min<size_t>(remainingFrames, DEFAULT_BUFFER_SIZE));
		const auto [inputFramesUsed, outputFramesGenerated] = state->m_resampler.process(
			{state->m_bufferView.data(), nativeChannels, static_cast<f_cnt_t>(state->m_bufferView.size() / nativeChannels)},
			{state->m_resampledBuffer.data(), nativeChannels, chunkFrames});

		if (inputFramesUsed == 0 && outputFramesGenerated == 0)
		{
			break;
		}

		state->m_bufferView = state->m_bufferView.subspan(inputFramesUsed * nativeChannels);

		for (f_cnt_t f = 0; f < outputFramesGenerated; ++f)
		{
			if (currentDstFrame + f < dst.frames())
			{
				convertFrame(state->m_resampledBuffer.data() + static_cast<size_t>(f) * nativeChannels,
					static_cast<f_cnt_t>(currentDstFrame + f));
			}
		}

		currentDstFrame += outputFramesGenerated;
		remainingFrames -= outputFramesGenerated;
	}

	return remainingFrames < Engine::audioEngine()->framesPerPeriod();
}

f_cnt_t Sample::renderNative(float* dst, f_cnt_t size, PlaybackState* state, Loop loop) const
{
	const auto nativeChannels = m_buffer->channels();
	const auto amp = m_amplification.load(std::memory_order_relaxed);

	for (f_cnt_t frame = 0; frame < size; ++frame)
	{
		switch (loop)
		{
		case Loop::Off:
			if (state->m_frameIndex < 0 || state->m_frameIndex >= m_endFrame) { return frame; }
			break;
		case Loop::On:
			if (state->m_frameIndex < m_loopStartFrame && state->m_backwards)
			{
				state->m_frameIndex = m_loopEndFrame - 1;
			}
			else if (state->m_frameIndex >= m_loopEndFrame) { state->m_frameIndex = m_loopStartFrame; }
			break;
		case Loop::PingPong:
			if (state->m_frameIndex < m_loopStartFrame && state->m_backwards)
			{
				state->m_frameIndex = m_loopStartFrame;
				state->m_backwards = false;
			}
			else if (state->m_frameIndex >= m_loopEndFrame)
			{
				state->m_frameIndex = m_loopEndFrame - 1;
				state->m_backwards = true;
			}
			break;
		default:
			break;
		}

		const auto sourceFrame
			= m_reversed ? m_buffer->size() - state->m_frameIndex - 1 : static_cast<size_t>(state->m_frameIndex);
		const auto* samples = m_buffer->interleavedData() + sourceFrame * nativeChannels;
		float* out = dst + frame * nativeChannels;
		for (ch_cnt_t ch = 0; ch < nativeChannels; ++ch)
		{
			out[ch] = samples[ch] * amp;
		}

		state->m_backwards ? --state->m_frameIndex : ++state->m_frameIndex;
	}

	return size;
}

f_cnt_t Sample::render(SampleFrame* dst, f_cnt_t size, PlaybackState* state, Loop loop) const
{
	for (f_cnt_t frame = 0; frame < size; ++frame)
	{
		switch (loop)
		{
		case Loop::Off:
			if (state->m_frameIndex < 0 || state->m_frameIndex >= m_endFrame) { return frame; }
			break;
		case Loop::On:
			if (state->m_frameIndex < m_loopStartFrame && state->m_backwards)
			{
				state->m_frameIndex = m_loopEndFrame - 1;
			}
			else if (state->m_frameIndex >= m_loopEndFrame) { state->m_frameIndex = m_loopStartFrame; }
			break;
		case Loop::PingPong:
			if (state->m_frameIndex < m_loopStartFrame && state->m_backwards)
			{
				state->m_frameIndex = m_loopStartFrame;
				state->m_backwards = false;
			}
			else if (state->m_frameIndex >= m_loopEndFrame)
			{
				state->m_frameIndex = m_loopEndFrame - 1;
				state->m_backwards = true;
			}
			break;
		default:
			break;
		}

		const auto sourceFrame
			= m_reversed ? m_buffer->size() - state->m_frameIndex - 1 : static_cast<size_t>(state->m_frameIndex);
		const auto* samples = m_buffer->interleavedData() + sourceFrame * m_buffer->channels();
		if (m_buffer->channels() == DEFAULT_CHANNELS)
		{
			dst[frame] = *reinterpret_cast<const SampleFrame*>(samples) * m_amplification;
		}
		else
		{
			float left = 0.f;
			float right = 0.f;
			downmixFrameToStereo(samples, m_buffer->layout(), left, right);
			dst[frame] = SampleFrame{left * m_amplification, right * m_amplification};
		}
		state->m_backwards ? --state->m_frameIndex : ++state->m_frameIndex;
	}

	return size;
}

auto Sample::sampleDuration() const -> std::chrono::milliseconds
{
	const auto numFrames = endFrame() - startFrame();
	const auto duration = numFrames / static_cast<float>(m_buffer->sampleRate()) * 1000;
	return std::chrono::milliseconds{static_cast<int>(duration)};
}

void Sample::setAllPointFrames(int startFrame, int endFrame, int loopStartFrame, int loopEndFrame)
{
	setStartFrame(startFrame);
	setEndFrame(endFrame);
	setLoopStartFrame(loopStartFrame);
	setLoopEndFrame(loopEndFrame);
}

} // namespace mxm
