/*
 * SampleTrack.h - class SampleTrack, a track which provides arrangement of samples
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

#ifndef MXM_SAMPLE_TRACK_H
#define MXM_SAMPLE_TRACK_H

#include <atomic>
#include <cstdint>
#include <memory>

#include <QPointer>

#include "AudioBusHandle.h"
#include "ComboBoxModel.h"
#include "Track.h"


namespace mxm
{

class SampleClip;
struct SampleRecordSession;

namespace gui
{

class SampleTrackView;
class SampleTrackWindow;

} // namespace gui


class SampleTrack : public Track
{
	Q_OBJECT
public:
	SampleTrack( TrackContainer* tc );
	~SampleTrack() override;

	bool play( const TimePos & _start, const f_cnt_t _frames,
						const f_cnt_t _frame_base, int _clip_num = -1 ) override;
	gui::TrackView * createView( gui::TrackContainerView* tcv ) override;
	Clip* createClip(const TimePos & pos) override;


	void saveTrackSpecificSettings(QDomDocument& doc, QDomElement& parent, bool presetMode) override;
	void loadTrackSpecificSettings( const QDomElement & _this ) override;

	bool isRecording() const { return m_recording.load(std::memory_order_relaxed); }
	bool startRecording();
	void stopRecording(bool stopTransport = true);
	int recordChannel() const { return m_recordChannel.load(std::memory_order_relaxed); }
	void setRecordChannel(int channel);
	bool isInputMonitoring() const { return m_inputMonitoring.load(std::memory_order_relaxed); }
	void setInputMonitoring(bool enabled) { m_inputMonitoring.store(enabled, std::memory_order_relaxed); }
	void updateInputLevel(float level);
	float takeInputLevel() { return m_inputLevel.exchange(0.f, std::memory_order_relaxed); }
	void updateRecordingClipLength();

	inline IntModel * mixerChannelModel()
	{
		return &m_mixerChannelModel;
	}

	//! @returns the selectable channel layouts of this track's output bus.
	ComboBoxModel* channelLayoutModel() { return &m_channelLayoutModel; }
	auto channelLayout() const -> const AudioChannelLayout&
	{
		return m_audioBusHandle.channelLayout();
	}

	inline AudioBusHandle* audioBusHandle()
	{
		return &m_audioBusHandle;
	}

	QString nodeName() const override
	{
		return "sampletrack";
	}

	bool isPlaying()
	{
		return m_isPlaying;
	}

	void setPlaying(bool playing)
	{
		if (m_isPlaying != playing) { emit playingChanged(); }
		m_isPlaying = playing;
	}

signals:
	void playingChanged();

public slots:
	void updateClips();
	void setPlayingClips( bool isPlaying );
	void updateMixerChannel();
	void updateChannelLayout();

private:
	FloatModel m_volumeModel;
	FloatModel m_panningModel;
	IntModel m_mixerChannelModel;
	ComboBoxModel m_channelLayoutModel;
	std::vector<AudioChannelLayout> m_layoutChoices;
	AudioBusHandle m_audioBusHandle;
	bool m_isPlaying;
	std::atomic_bool m_recording{false};
	std::atomic_int m_recordChannel{0};
	std::atomic_bool m_inputMonitoring{false};
	std::atomic<float> m_inputLevel{0.f};
	std::shared_ptr<SampleRecordSession> m_recordingSession;
	QPointer<SampleClip> m_recordingClip;



	friend class gui::SampleTrackView;
	friend class gui::SampleTrackWindow;

} ;


} // namespace mxm

#endif // MXM_SAMPLE_TRACK_H
