/*
 * SampleCapture.h - real-time capture queue with asynchronous disk backing
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

#ifndef MXM_SAMPLE_CAPTURE_H
#define MXM_SAMPLE_CAPTURE_H

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <QString>

#include "AudioBuffer.h"
#include "AudioBufferView.h"
#include "AudioChannelLayout.h"
#include "SampleBuffer.h"

class QTemporaryFile;

namespace mxm
{

/**
 * A bounded, preallocated capture queue that drains asynchronously to disk.
 *
 * The audio thread only copies frames into one of the preallocated blocks of the
 * ring buffer. A writer thread interleaves and writes completed blocks to a
 * temporary WAV file, so take length is not limited by memory and the audio
 * callback never performs file I/O or heap allocation.
 *
 * `write()` reports queue overflow through `droppedFrames()`. `finish()` stops
 * the writer, loads the take back into a `SampleBuffer` and preserves the actual
 * capture channel layout. Recording finalization is explicit and belongs on the
 * model/UI thread.
 */
class MXM_EXPORT SampleCapture
{
public:
	/**
	 * @param sampleRate capture sample rate
	 * @param layout capture channel count and speaker roles
	 * @param blockFrames frames per ring block; should be at least one audio period
	 * @param blockCount number of preallocated ring blocks
	 */
	SampleCapture(sample_rate_t sampleRate, AudioChannelLayout layout, f_cnt_t blockFrames,
		size_t blockCount = 64);
	~SampleCapture();

	SampleCapture(const SampleCapture&) = delete;
	SampleCapture& operator=(const SampleCapture&) = delete;

	//! Real-time safe. Copies every source channel in order.
	void write(PlanarBufferView<const float> frames);

	/**
	 * @brief Writes @a frames into explicit destination channels.
	 *
	 * `destinationChannels[i]` receives source channel `i`. Destinations outside
	 * the capture layout are ignored and unassigned capture channels are
	 * zero-filled. This is the channel-aware path used by per-track input
	 * assignments.
	 */
	void write(PlanarBufferView<const float> frames, const std::vector<ch_cnt_t>& destinationChannels);

	/**
	 * @brief Stops the writer and returns the recorded take.
	 *
	 * May be called from the UI thread. Returns an empty buffer if nothing was
	 * recorded or if the disk writer failed.
	 */
	auto finish() -> std::shared_ptr<const SampleBuffer>;

	auto channels() const -> ch_cnt_t { return m_layout.channels(); }
	auto layout() const -> const AudioChannelLayout& { return m_layout; }
	auto sampleRate() const -> sample_rate_t { return m_sampleRate; }

	auto recordedFrames() const -> std::uint64_t { return m_recordedFrames.load(std::memory_order_relaxed); }
	auto droppedFrames() const -> std::uint64_t { return m_droppedFrames.load(std::memory_order_relaxed); }
	auto diskError() const -> bool { return m_diskError.load(std::memory_order_relaxed); }
	auto errorString() const -> QString;

private:
	void writerLoop();
	void copyBlock(PlanarBufferView<const float> frames, const std::vector<ch_cnt_t>& destinationChannels);
	bool openWriter();

	AudioChannelLayout m_layout;
	sample_rate_t m_sampleRate;
	f_cnt_t m_blockFrames;
	size_t m_blockCount;

	//! Planar ring storage; each block is a frame offset into every channel.
	std::unique_ptr<AudioBuffer> m_buffer;
	std::vector<f_cnt_t> m_blockFrameCounts;
	std::vector<float> m_interleaveScratch;

	std::atomic<std::uint32_t> m_writeIndex{0};
	std::atomic<std::uint32_t> m_readIndex{0};
	std::atomic<std::uint64_t> m_recordedFrames{0};
	std::atomic<std::uint64_t> m_droppedFrames{0};
	std::atomic_bool m_accepting{true};
	std::atomic_bool m_diskError{false};

	std::unique_ptr<QTemporaryFile> m_tempFile;

	std::thread m_writerThread;
	mutable std::mutex m_wakeMutex;
	std::condition_variable m_wake;

	// guarded by m_wakeMutex
	QString m_errorString;

	bool m_started = false;
	std::atomic_bool m_finished{false};
};

} // namespace mxm

#endif // MXM_SAMPLE_CAPTURE_H
