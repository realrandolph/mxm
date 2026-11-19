/*
 * SampleBuffer.h - container-class SampleBuffer
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

#ifndef MXM_SAMPLE_BUFFER_H
#define MXM_SAMPLE_BUFFER_H

#include <QString>
#include <memory>
#include <span>
#include <vector>

#include "AudioBufferView.h"
#include "AudioChannelLayout.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "MxmTypes.h"
#include "mxm_export.h"

namespace mxm {

/**
 * Immutable sample data with variable channel count, channel layout and sample rate.
 *
 * The audio data is stored interleaved. Frame count, sample rate, channel count,
 * channel order and channel layout are separate pieces of metadata. For stereo
 * buffers the storage is bit-compatible with `SampleFrame` arrays, which keeps
 * the existing stereo processing and serialization paths working.
 */
class MXM_EXPORT SampleBuffer
{
public:
	SampleBuffer() = default;

	//! Creates a stereo buffer from legacy sample frames.
	SampleBuffer(std::vector<SampleFrame> data, int sampleRate, const QString& audioFile = "");
	SampleBuffer(const SampleFrame* data, size_t numFrames,
		int sampleRate = Engine::audioEngine()->outputSampleRate());

	/**
	 * Creates a buffer from interleaved samples.
	 *
	 * @param samples interleaved samples, `frames * channels` elements
	 * @param channels channel count
	 * @param layout speaker assignment; its channel count must match @a channels
	 * @param sampleRate sample rate in Hz
	 * @param audioFile source file, if the data came from a file
	 */
	SampleBuffer(std::vector<float> samples, ch_cnt_t channels, AudioChannelLayout layout, int sampleRate,
		const QString& audioFile = "");

	static auto fromInterleaved(const float* samples, f_cnt_t frames, ch_cnt_t channels,
		AudioChannelLayout layout, int sampleRate, const QString& audioFile = "")
		-> std::shared_ptr<SampleBuffer>;

	static auto fromPlanar(PlanarBufferView<const float> samples, AudioChannelLayout layout, int sampleRate,
		const QString& audioFile = "") -> std::shared_ptr<SampleBuffer>;

	friend void swap(SampleBuffer& first, SampleBuffer& second) noexcept;
	auto toBase64() const -> QString;

	auto audioFile() const -> const QString& { return m_audioFile; }
	auto sampleRate() const -> sample_rate_t { return m_sampleRate; }

	auto interleavedData() const -> const float* { return m_samples.data(); }
	auto interleavedData() -> float* { return m_samples.data(); }

	auto interleavedView() const -> InterleavedBufferView<const float>
	{
		return {m_samples.data(), m_channels, m_frames};
	}

	auto interleavedView() -> InterleavedBufferView<float>
	{
		return {m_samples.data(), m_channels, m_frames};
	}

	//! @returns the sample at the given frame and channel
	auto sample(f_cnt_t frame, ch_cnt_t channel) const -> float
	{
		assert(frame < m_frames && channel < m_channels);
		return m_samples[frame * m_channels + channel];
	}

	/**
	 * Legacy stereo accessor.
	 *
	 * @warning Only valid for stereo buffers. New code should use
	 *          `interleavedView()` or `sample()`. Mono and multichannel buffers
	 *          cannot be represented as an array of `SampleFrame`.
	 */
	auto data() const -> const SampleFrame*
	{
		assert(m_channels == DEFAULT_CHANNELS);
		return reinterpret_cast<const SampleFrame*>(m_samples.data());
	}

	//! @returns frame count (one sample per channel per frame)
	auto size() const -> size_t { return static_cast<size_t>(m_frames); }
	auto frames() const -> f_cnt_t { return m_frames; }
	auto empty() const -> bool { return m_frames == 0 || m_channels == 0; }
	auto channels() const -> ch_cnt_t { return m_channels; }
	auto layout() const -> const AudioChannelLayout& { return m_layout; }

	static auto emptyBuffer() -> std::shared_ptr<const SampleBuffer>;

	static std::shared_ptr<const SampleBuffer> fromFile(const QString& path);
	static std::shared_ptr<const SampleBuffer> fromBase64(const QString& str,
		int sampleRate = Engine::audioEngine()->outputSampleRate(), ch_cnt_t channels = DEFAULT_CHANNELS,
		const QString& layout = {});

private:
	//! Interleaved samples, `m_frames * m_channels` elements
	std::vector<float> m_samples;
	QString m_audioFile;
	sample_rate_t m_sampleRate = Engine::audioEngine()->outputSampleRate();
	ch_cnt_t m_channels = 0;
	AudioChannelLayout m_layout;
	f_cnt_t m_frames = 0;
};

} // namespace mxm

#endif // MXM_SAMPLE_BUFFER_H
