/*
 * AudioFileDevice.cpp - base-class for audio-device-classes which write
 *                       their output into a file
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

#include <QMessageBox>

#include "AudioChannelBuffer.h"
#include "AudioFileDevice.h"
#include "ExportProjectDialog.h"
#include "GuiApplication.h"

namespace mxm
{

AudioFileDevice::AudioFileDevice( OutputSettings const & outputSettings,
					const AudioChannelLayout& layout,
					const QString & _file,
					AudioEngine*  _audioEngine ) :
	AudioDevice( layout.channels(), _audioEngine ),
	m_outputFile( _file ),
	m_outputSettings(outputSettings),
	m_channelLayout(layout),
	m_conversionBuffer(nullptr)
{
	using gui::ExportProjectDialog;

	setSampleRate( outputSettings.getSampleRate() );

	if( m_outputFile.open( QFile::WriteOnly | QFile::Truncate ) == false )
	{
		QString title, message;
		title = ExportProjectDialog::tr( "Could not open file" );
		message = ExportProjectDialog::tr( "Could not open file %1 "
						"for writing.\nPlease make "
						"sure you have write "
						"permission to the file and "
						"the directory containing the "
						"file and try again!"
								).arg( _file );

		if (gui::getGUI() != nullptr)
		{
			QMessageBox::critical( nullptr, title, message,
						QMessageBox::Ok,
						QMessageBox::NoButton );
		}
		else
		{
			fprintf( stderr, "%s\n", message.toUtf8().constData() );
			exit( EXIT_FAILURE );
		}
	}
}




AudioFileDevice::~AudioFileDevice()
{
	m_outputFile.close();
}




void AudioFileDevice::writeBuffer(PlanarBufferView<const float> buffer,
	const AudioChannelLayout& sourceLayout)
{
	if (buffer.empty())
	{
		writeBufferImpl({});
		return;
	}

	if (sourceLayout == m_channelLayout)
	{
		writeBufferImpl(buffer);
		return;
	}

	if (!m_conversionBuffer)
	{
		m_conversionBuffer = createChannelBuffer(buffer.frames(), m_channelLayout);
	}

	auto converted = logicalBuffers(*m_conversionBuffer, m_channelLayout);
	m_conversionBuffer->silenceAllChannels();
	addPlanarConverted(buffer, sourceLayout, converted, m_channelLayout);
	writeBufferImpl(converted);
}




int AudioFileDevice::writeData( const void* data, int len )
{
	if( m_outputFile.isOpen() )
	{
		return m_outputFile.write( (const char *) data, len );
	}

	return -1;
}

} // namespace mxm
