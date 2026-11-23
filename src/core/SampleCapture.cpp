/*
 * SampleCapture.cpp - real-time capture queue with asynchronous disk backing
 *
 * Copyright (c) 2026 MXM contributors
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include "SampleCapture.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <QDir>
#include <QTemporaryFile>

#include <sndfile.h>

#include "SampleDecoder.h"

namespace mxm
{

namespace
{
constexpr auto WavFormat = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
}

SampleCapture::SampleCapture(
	sample_rate_t sampleRate, AudioChannelLayout layout, f_cnt_t blockFrames, size_t blockCount)
	: m_layout(std::move(layout))
	, m_sampleRate(sampleRate)
	, m_blockFrames(std::max<f_cnt_t>(blockFrames, 1))
	, m_blockCount(std::max<size_t>(blockCount, 2))
	, m_buffer(std::make_unique<AudioBuffer>(m_blockFrames * m_blockCount, m_layout.channels()))
	, m_blockFrameCounts(m_blockCount, 0)
	, m_interleaveScratch(static_cast<size_t>(m_blockFrames) * m_layout.channels())
{
	Q_ASSERT(m_layout.channels() > 0);

	m_tempFile = std::make_unique<QTemporaryFile>(
		QDir::tempPath() + QStringLiteral("/mxm-capture-XXXXXX.wav"));
	if (!m_tempFile->open())
	{
		m_diskError.store(true, std::memory_order_relaxed);
		m_errorString = QStringLiteral("could not create a temporary recording file");
		return;
	}
	m_tempFile->close();

	m_started = openWriter();
	if (!m_started) { return; }

	m_writerThread = std::thread{[this] { writerLoop(); }};
}

SampleCapture::~SampleCapture()
{
	finish();
}

auto SampleCapture::errorString() const -> QString
{
	const auto lock = std::lock_guard{m_wakeMutex};
	return m_errorString;
}

void SampleCapture::write(PlanarBufferView<const float> frames)
{
	if (!m_accepting.load(std::memory_order_acquire) || frames.empty() || frames.channels() == 0) { return; }
	copyBlock(frames, {});
}

void SampleCapture::write(PlanarBufferView<const float> frames,
	const std::vector<ch_cnt_t>& destinationChannels)
{
	if (!m_accepting.load(std::memory_order_acquire) || frames.empty() || frames.channels() == 0) { return; }
	copyBlock(frames, destinationChannels);
}

void SampleCapture::copyBlock(PlanarBufferView<const float> frames,
	const std::vector<ch_cnt_t>& destinationChannels)
{
	const auto totalFrames = frames.frames();
	f_cnt_t frameOffset = 0;

	while (frameOffset < totalFrames)
	{
		const auto write = m_writeIndex.load(std::memory_order_relaxed);
		const auto next = (write + 1) % static_cast<std::uint32_t>(m_blockCount);
		if (next == m_readIndex.load(std::memory_order_acquire))
		{
			m_droppedFrames.fetch_add(totalFrames - frameOffset, std::memory_order_relaxed);
			return;
		}

		const auto frameCount = std::min<f_cnt_t>(totalFrames - frameOffset, m_blockFrames);
		for (ch_cnt_t channel = 0; channel < m_layout.channels(); ++channel)
		{
			auto* dst = m_buffer->buffer(channel).data() + write * m_blockFrames;
			std::fill_n(dst, frameCount, 0.f);
		}

		const auto identity = destinationChannels.empty();
		const auto mapped = identity ? static_cast<std::size_t>(std::min(frames.channels(), m_layout.channels()))
			: std::min<std::size_t>(frames.channels(), destinationChannels.size());
		for (size_t sourceChannel = 0; sourceChannel < mapped; ++sourceChannel)
		{
			const auto destination = identity ? static_cast<ch_cnt_t>(sourceChannel)
				: destinationChannels[sourceChannel];
			if (destination >= m_layout.channels()) { continue; }
			auto* dst = m_buffer->buffer(destination).data() + write * m_blockFrames;
			const auto* src = frames.bufferPtr(static_cast<ch_cnt_t>(sourceChannel)) + frameOffset;
			std::copy_n(src, frameCount, dst);
		}

		m_blockFrameCounts[write] = frameCount;
		m_writeIndex.store(next, std::memory_order_release);
		m_recordedFrames.fetch_add(frameCount, std::memory_order_relaxed);
		m_wake.notify_one();
		frameOffset += frameCount;
	}
}




bool SampleCapture::openWriter()
{
	auto info = SF_INFO{};
	info.samplerate = static_cast<int>(m_sampleRate);
	info.channels = static_cast<int>(m_layout.channels());
	info.format = WavFormat;

	auto* file = sf_open(m_tempFile->fileName().toUtf8().constData(), SFM_WRITE, &info);
	if (file == nullptr)
	{
		m_diskError.store(true, std::memory_order_relaxed);
		m_errorString = QStringLiteral("could not open the temporary recording file");
		return false;
	}
	sf_close(file);
	return true;
}

void SampleCapture::writerLoop()
{
	auto info = SF_INFO{};
	info.samplerate = static_cast<int>(m_sampleRate);
	info.channels = static_cast<int>(m_layout.channels());
	info.format = WavFormat;

	auto* file = sf_open(m_tempFile->fileName().toUtf8().constData(), SFM_WRITE, &info);
	if (file == nullptr)
	{
		m_diskError.store(true, std::memory_order_relaxed);
		m_wake.notify_all();
		return;
	}

	while (true)
	{
		auto read = m_readIndex.load(std::memory_order_relaxed);
		auto write = m_writeIndex.load(std::memory_order_acquire);
		if (read == write)
		{
			if (!m_accepting.load(std::memory_order_acquire)) { break; }

			auto lock = std::unique_lock{m_wakeMutex};
			// The producer does not take this mutex, so use a bounded wait to
			// avoid a lost wakeup if a block is published between the check and
			// the wait. This never affects the audio callback.
			m_wake.wait_for(lock, std::chrono::milliseconds{100}, [this] {
				return m_readIndex.load(std::memory_order_relaxed) != m_writeIndex.load(std::memory_order_acquire)
					|| !m_accepting.load(std::memory_order_acquire);
			});
			continue;
		}

		const auto frameCount = m_blockFrameCounts[read];
		const auto channels = static_cast<size_t>(m_layout.channels());
		for (size_t frame = 0; frame < frameCount; ++frame)
		{
			for (size_t channel = 0; channel < channels; ++channel)
			{
				m_interleaveScratch[frame * channels + channel]
					= m_buffer->buffer(static_cast<ch_cnt_t>(channel)).data()[read * m_blockFrames + frame];
			}
		}

		if (sf_writef_float(file, m_interleaveScratch.data(), static_cast<sf_count_t>(frameCount))
			!= static_cast<sf_count_t>(frameCount))
		{
			if (!m_diskError.exchange(true, std::memory_order_relaxed))
			{
				const auto lock = std::lock_guard{m_wakeMutex};
				m_errorString = QStringLiteral("writing the recording to disk failed");
			}
		}

		m_readIndex.store((read + 1) % static_cast<std::uint32_t>(m_blockCount), std::memory_order_release);
	}

	if (sf_close(file) != 0)
	{
		m_diskError.store(true, std::memory_order_relaxed);
		const auto lock = std::lock_guard{m_wakeMutex};
		if (m_errorString.isEmpty()) { m_errorString = QStringLiteral("finalizing the recording file failed"); }
	}
	m_wake.notify_all();
}

auto SampleCapture::finish() -> std::shared_ptr<const SampleBuffer>
{
	if (m_finished.exchange(true, std::memory_order_acq_rel)) { return SampleBuffer::emptyBuffer(); }

	m_accepting.store(false, std::memory_order_release);
	m_wake.notify_all();
	if (m_writerThread.joinable()) { m_writerThread.join(); }

	if (!m_started || m_diskError.load(std::memory_order_relaxed) || recordedFrames() == 0)
	{
		return SampleBuffer::emptyBuffer();
	}

	const auto result = SampleDecoder::decode(m_tempFile->fileName());
	if (!result) { return SampleBuffer::emptyBuffer(); }

	return std::make_shared<SampleBuffer>(std::move(result->samples), m_layout.channels(), m_layout,
		static_cast<int>(m_sampleRate));
}

} // namespace mxm
