/*
 * AudioSndio.h - base-class that implements sndio audio support
 *
 * Copyright (c) 2010-2016 jackmsr@openbsd.net
 * Copyright (c) 2016-2017 David Carlier <devnexen@gmail.com>
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

#ifndef MXM_AUDIO_SNDIO_H
#define MXM_AUDIO_SNDIO_H

#include "mxmconfig.h"

#ifdef MXM_HAVE_SNDIO

#include <QThread>
#include <sndio.h>

#include <atomic>
#include <memory>
#include <unordered_map>
#include <vector>

#include "AudioDevice.h"
#include "AudioDeviceSetupWidget.h"

class QLineEdit;

namespace mxm
{

namespace gui
{
class LcdSpinBox;
}


class AudioSndio : public QThread, public AudioDevice
{
	Q_OBJECT
public:
	AudioSndio( bool & _success_ful, AudioEngine * _audioEngine );
	~AudioSndio() override;

	inline static QString name()
	{
		return QT_TRANSLATE_NOOP( "AudioDeviceSetupWidget", "sndio" );
	}

	static QString resolvedDefaultDevice();

	class setupWidget : public gui::AudioDeviceSetupWidget
	{
	public:
		setupWidget( QWidget * _parent );
		~setupWidget() override = default;

		void saveSettings() override;

	private:
		QLineEdit * m_device;
		gui::LcdSpinBox * m_channels;
	} ;

private:
	void startProcessingImpl() override;
	void stopProcessingImpl() override;
	void run() override;

	auto supportsTrackInputPorts() const -> bool override;
	auto registerTrackInputPort(quint64 id, const QString& name, ch_cnt_t channels) -> bool override;
	void unregisterTrackInputPort(quint64 id) override;
	auto inputSources() const -> std::vector<AudioInputSource> override;
	void setTrackInputSource(quint64 id, const QStringList& endpointPorts) override;

	struct TrackCapture;

	void startTrackCapture(TrackCapture& capture);
	void stopTrackCapture(TrackCapture& capture);
	void stopTrackCaptures();
	void captureThread(TrackCapture& capture);

	struct sio_hdl *m_hdl;
	struct sio_par m_par;
	QString m_configuredDevice;
	QString m_monitoredDevice;
	std::atomic_bool m_errorReported{false};
	std::unordered_map<quint64, std::unique_ptr<TrackCapture>> m_trackCaptures;
};

} // namespace mxm

#endif // MXM_HAVE_SNDIO

#endif // MXM_AUDIO_SNDIO_H
