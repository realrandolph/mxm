/*
 * AudioPulseAudio.h - device-class which implements PulseAudio-output
 *
 * Copyright (c) 2008-2009 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef MXM_AUDIO_PULSEAUDIO_H
#define MXM_AUDIO_PULSEAUDIO_H

#include "mxmconfig.h"

#ifdef MXM_HAVE_PULSEAUDIO

#include <pulse/pulseaudio.h>
#include <mutex>
#include <QSemaphore>
#include <QThread>

#include <atomic>

#include "AudioDevice.h"
#include "AudioDeviceSetupWidget.h"

class QLineEdit;
class QShowEvent;

namespace mxm
{

namespace gui
{
class LcdSpinBox;
}


class AudioPulseAudio : public QThread, public AudioDevice
{
	Q_OBJECT
public:
	AudioPulseAudio( bool & _success_ful, AudioEngine* audioEngine );

	inline static QString name()
	{
		return QT_TRANSLATE_NOOP( "AudioDeviceSetupWidget", "PulseAudio" );
	}

	static QString probeDevice();
	static QString resolvedDefaultDevice();


	class setupWidget : public gui::AudioDeviceSetupWidget
	{
	public:
		setupWidget( QWidget * _parent );
		~setupWidget() override;

		void saveSettings() override;

	protected:
		void showEvent( QShowEvent * _event ) override;

	private:
		QLineEdit * m_device;
		gui::LcdSpinBox * m_channels;

	} ;


	void streamWriteCallback( pa_stream * s, size_t length );
	void streamMovedCallback( pa_stream * s );
	void streamStateCallback( pa_stream * s );
	void contextStateCallback( pa_context * c );

	void signalConnected( bool connected );

	pa_stream * m_s;
	pa_sample_spec m_sampleSpec;
	double m_latency;

private:
	void startProcessingImpl() override;
	void stopProcessingImpl() override;
	void run() override;
	QString monitoredDevice() const;
	void setMonitoredDevice(const QString& device);

	bool m_connected;
	QSemaphore m_connectedSemaphore;
	mutable std::mutex m_monitoredDeviceMutex;
	QString m_monitoredDevice;
	std::atomic_bool m_streamFailureReported{false};

} ;

} // namespace mxm

#endif // MXM_HAVE_PULSEAUDIO

#endif // MXM_AUDIO_PULSEAUDIO_H
