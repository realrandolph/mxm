/*
 * AudioFileDevice.h - base-class for audio-device-classes which write
 *                     their output into a file
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#ifndef MXM_AUDIO_FILE_DEVICE_H
#define MXM_AUDIO_FILE_DEVICE_H

#include <memory>

#include <QFile>

#include "AudioBuffer.h"
#include "AudioChannelLayout.h"
#include "AudioDevice.h"
#include "OutputSettings.h"

namespace mxm
{

class AudioFileDevice : public AudioDevice
{
public:
	AudioFileDevice(OutputSettings const & outputSettings,
			const AudioChannelLayout& layout, const QString & _file,
			AudioEngine* audioEngine );
	~AudioFileDevice() override;

	QString outputFile() const
	{
		return m_outputFile.fileName();
	}

	OutputSettings const & getOutputSettings() const { return m_outputSettings; }

	//! @returns the on-disk channel layout selected for this file format.
	auto channelLayout() const -> const AudioChannelLayout& { return m_channelLayout; }

	/**
	 * @brief Writes a planar block supplied in @a sourceLayout into the file.
	 *
	 * When the source and file layouts differ, the block is converted with
	 * explicit speaker-role mapping first; channels are never silently
	 * truncated.
	 */
	void writeBuffer(PlanarBufferView<const float> buffer, const AudioChannelLayout& sourceLayout);

protected:
	int writeData( const void* data, int len );

	inline bool outputFileOpened() const
	{
		return m_outputFile.isOpen();
	}

	inline int outputFileHandle() const
	{
		return m_outputFile.handle();
	}

	//! Encodes a block that already uses @ref channelLayout().
	virtual void writeBufferImpl(PlanarBufferView<const float> buffer) = 0;

private:
	void startProcessingImpl() override {}
	void stopProcessingImpl() override {}

	QFile m_outputFile;
	OutputSettings m_outputSettings;
	AudioChannelLayout m_channelLayout;
	std::unique_ptr<AudioBuffer> m_conversionBuffer;
} ;

using AudioFileDeviceInstantiaton
	= AudioFileDevice* (*)(const QString&, const OutputSettings&, const AudioChannelLayout&,
		AudioEngine*, bool&);

} // namespace mxm

#endif // MXM_AUDIO_FILE_DEVICE_H
