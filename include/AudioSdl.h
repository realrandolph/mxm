/*
 * AudioSdl.h - device-class that performs PCM-output via SDL
 *
 * Copyright (c) 2004-2009 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef MXM_AUDIO_SDL_H
#define MXM_AUDIO_SDL_H

#include "mxmconfig.h"

#ifdef MXM_HAVE_SDL

#include <SDL2/SDL_audio.h>
#include <SDL2/SDL_events.h>

#include <QTimer>

#include <atomic>
#include <memory>
#include <unordered_map>
#include <vector>

#include "AudioDevice.h"
#include "AudioDeviceSetupWidget.h"

class QComboBox;
class QHideEvent;
class QShowEvent;

namespace mxm
{

class AudioSdl : public AudioDevice
{
public:
	AudioSdl( bool & _success_ful, AudioEngine* audioEngine );
	~AudioSdl() override;

	inline static QString name()
	{
		return QT_TRANSLATE_NOOP( "AudioDeviceSetupWidget",
					"SDL (Simple DirectMedia Layer)" );
	}


	class setupWidget : public gui::AudioDeviceSetupWidget
	{
	public:
		setupWidget( QWidget * _parent );
		~setupWidget() override = default;

	void saveSettings() override;
	
	private:
		void populatePlaybackDeviceComboBox();
		void updateDefaultDeviceInfo();
		void showEvent(QShowEvent* event) override;
		void hideEvent(QHideEvent* event) override;

	private:
		QComboBox* m_playbackDeviceComboBox = nullptr;
		QTimer m_defaultDeviceInfoTimer;

		static QString s_systemDefaultDevice;
	} ;


private:
	void startProcessingImpl() override;
	void stopProcessingImpl() override;
	auto supportsTrackInputPorts() const -> bool override { return m_supportsCapture; }
	auto registerTrackInputPort(quint64 id, const QString& name, ch_cnt_t channels) -> bool override;
	void unregisterTrackInputPort(quint64 id) override;
	void renameTrackInputPort(quint64 id, const QString& name) override;
	auto inputSources() const -> std::vector<AudioInputSource> override;
	void setTrackInputSource(quint64 id, const QStringList& endpointPorts) override;
	void checkDeviceRoutes();
	static int sdlAudioEventWatch(void* data, SDL_Event* event);

	static void sdlAudioCallback( void * _udata, Uint8 * _buf, int _len );
	void sdlAudioCallback( Uint8 * _buf, int _len );

	struct TrackCapture;
	static void sdlTrackInputCallback(void* data, Uint8* buffer, int length);
	void sdlTrackInputCallback(TrackCapture& capture, Uint8* buffer, int length);

	SDL_AudioSpec m_audioHandle;
	SDL_AudioDeviceID m_outputDevice;

	QString m_configuredPlaybackDevice;
	QString m_activePlaybackDevice;
	QString m_monitoredPlaybackDevice;
	bool m_activePlaybackUsesDefault = false;
	std::atomic_bool m_playbackDeviceRemoved{false};
	std::atomic_bool m_playbackRemovalReported{false};
	QTimer m_deviceMonitor;
	std::unordered_map<quint64, std::unique_ptr<TrackCapture>> m_trackCaptures;
} ;


} // namespace mxm

#endif // MXM_HAVE_SDL

#endif // MXM_AUDIO_SDL_H
