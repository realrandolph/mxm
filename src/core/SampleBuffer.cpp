/*
 * SampleBuffer.cpp - container-class SampleBuffer
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

#include "SampleBuffer.h"

#include <QDebug>
#include <QMessageBox>
#include <cstring>
#include <stdexcept>

#include "GuiApplication.h"
#include "PathUtil.h"
#include "SampleDecoder.h"

namespace mxm {

SampleBuffer::SampleBuffer(const SampleFrame* data, size_t numFrames, int sampleRate)
	: m_samples(reinterpret_cast<const float*>(data), reinterpret_cast<const float*>(data) + numFrames * DEFAULT_CHANNELS)
	, m_sampleRate(sampleRate)
	, m_channels(DEFAULT_CHANNELS)
	, m_layout(AudioChannelLayout::stereo())
	, m_frames(numFrames)
{
}

SampleBuffer::SampleBuffer(std::vector<SampleFrame> data, int sampleRate, const QString& audioFile)
	: m_samples(reinterpret_cast<const float*>(data.data()),
		  reinterpret_cast<const float*>(data.data()) + data.size() * DEFAULT_CHANNELS)
	, m_audioFile(audioFile)
	, m_sampleRate(sampleRate)
	, m_channels(DEFAULT_CHANNELS)
	, m_layout(AudioChannelLayout::stereo())
	, m_frames(data.size())
{
}

SampleBuffer::SampleBuffer(
	std::vector<float> samples, ch_cnt_t channels, AudioChannelLayout layout, int sampleRate, const QString& audioFile)
	: m_samples(std::move(samples))
	, m_audioFile(audioFile)
	, m_sampleRate(sampleRate)
	, m_channels(channels)
	, m_layout(std::move(layout))
{
	if (m_layout.channels() != m_channels)
	{
		// Keep the channel count authoritative and make the missing speaker
		// roles explicit rather than silently dropping channels.
		m_layout = m_layout.channels() == 0 ? AudioChannelLayout::discrete(m_channels) : m_layout;
		if (m_layout.channels() != m_channels) { throw std::invalid_argument{"sample channel layout mismatch"}; }
	}
	if (m_channels == 0 || m_samples.size() % m_channels != 0)
	{
		throw std::invalid_argument{"sample data does not match its channel count"};
	}
	m_frames = m_samples.size() / m_channels;
}

auto SampleBuffer::fromInterleaved(const float* samples, f_cnt_t frames, ch_cnt_t channels,
	AudioChannelLayout layout, int sampleRate, const QString& audioFile) -> std::shared_ptr<SampleBuffer>
{
	const auto count = static_cast<std::size_t>(frames) * channels;
	return std::make_shared<SampleBuffer>(std::vector<float>(samples, samples + count), channels,
		std::move(layout), sampleRate, audioFile);
}

auto SampleBuffer::fromPlanar(PlanarBufferView<const float> samples, AudioChannelLayout layout, int sampleRate,
	const QString& audioFile) -> std::shared_ptr<SampleBuffer>
{
	if (samples.channels() != layout.channels()) { throw std::invalid_argument{"planar layout mismatch"}; }

	auto interleaved = std::vector<float>(static_cast<std::size_t>(samples.frames()) * samples.channels());
	for (f_cnt_t frame = 0; frame < samples.frames(); ++frame)
	{
		auto* dst = interleaved.data() + frame * samples.channels();
		for (ch_cnt_t channel = 0; channel < samples.channels(); ++channel)
		{
			dst[channel] = samples.bufferPtr(channel)[frame];
		}
	}
	return std::make_shared<SampleBuffer>(
		std::move(interleaved), samples.channels(), std::move(layout), sampleRate, audioFile);
}

void swap(SampleBuffer& first, SampleBuffer& second) noexcept
{
	using std::swap;
	swap(first.m_samples, second.m_samples);
	swap(first.m_audioFile, second.m_audioFile);
	swap(first.m_sampleRate, second.m_sampleRate);
	swap(first.m_channels, second.m_channels);
	swap(first.m_layout, second.m_layout);
	swap(first.m_frames, second.m_frames);
}

QString SampleBuffer::toBase64() const
{
	// TODO: Replace with non-Qt equivalent
	const auto data = reinterpret_cast<const char*>(m_samples.data());
	const auto size = static_cast<int>(m_samples.size() * sizeof(float));
	const auto byteArray = QByteArray{data, size};
	return byteArray.toBase64();
}

auto SampleBuffer::emptyBuffer() -> std::shared_ptr<const SampleBuffer>
{
	static auto s_buffer = std::make_shared<const SampleBuffer>();
	return s_buffer;
}

std::shared_ptr<const SampleBuffer> SampleBuffer::fromFile(const QString& filePath)
{
	if (filePath.isEmpty()) { return SampleBuffer::emptyBuffer(); }

	const auto absolutePath = PathUtil::toAbsolute(filePath);
	const auto storedPath = PathUtil::toShortestRelative(filePath);

	auto result = SampleDecoder::decode(absolutePath);

	if (!result)
	{
		// TODO: Improve error handling. We dont always want to show a message box on failure when there is a GUI (e.g.
		// when loading the project), and this function also shouldn't be concerned with handling the error.
		if (gui::getGUI())
		{
			QMessageBox::warning(nullptr, QObject::tr("Failed to load sample"),
				QObject::tr("The sample may be corrupted or unsupported."));
		}
		else
		{
			qWarning() << QObject::tr(
				"Failed to load sample at path %1, the file may not exist, be corrupted, or is unsupported.")
							  .arg(absolutePath);
		}

		return SampleBuffer::emptyBuffer();
	}

	auto [samples, channels, layout, sampleRate] = std::move(*result);
	return std::make_shared<SampleBuffer>(
		std::move(samples), channels, std::move(layout), sampleRate, storedPath);
}

std::shared_ptr<const SampleBuffer> SampleBuffer::fromBase64(
	const QString& str, int sampleRate, ch_cnt_t channels, const QString& layout)
{
	if (str.isEmpty()) { return SampleBuffer::emptyBuffer(); }
	if (channels == 0 || channels > MaxChannelsPerAudioBuffer)
	{
		qWarning() << QObject::tr("Failed to load Base64 sample, invalid channel count");
		return SampleBuffer::emptyBuffer();
	}

	const auto bytes = QByteArray::fromBase64(str.toUtf8());

	if (bytes.size() % static_cast<int>(sizeof(float) * channels) != 0)
	{
		// TODO: Improve error handling. We dont always want to show a message box on failure when there is a GUI (e.g.
		// when loading the project), and this function also shouldn't be concerned with handling the error.
		if (gui::getGUI())
		{
			QMessageBox::warning(
				nullptr, QObject::tr("Failed to load sample"), QObject::tr("The sample size is invalid."));
		}
		else
		{
			qWarning() << QObject::tr("Failed to load Base64 sample, invalid size");
		}

		return SampleBuffer::emptyBuffer();
	}

	auto samples = std::vector<float>(bytes.size() / sizeof(float));
	std::memcpy(samples.data(), bytes.constData(), bytes.size());

	return std::make_shared<SampleBuffer>(std::move(samples), channels,
		AudioChannelLayout::fromString(layout, channels), sampleRate);
}

} // namespace mxm
