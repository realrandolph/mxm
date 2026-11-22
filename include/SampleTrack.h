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
#include <optional>
#include <vector>

#include <QPointer>

#include "AudioBusHandle.h"
#include "AudioInputSource.h"
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
	void setName(const QString& newName) override;

	bool isRecording() const { return m_recording.load(std::memory_order_relaxed); }
	bool startRecording();
	void stopRecording(bool stopTransport = true);
	int recordChannel() const { return m_recordChannel.load(std::memory_order_relaxed); }
	void setRecordChannel(int channel);

	//! @returns the saved input endpoint id for this track.
	auto inputSourceId() const -> const QString& { return m_inputSourceId; }

	/**
	 * @brief Assigns this track's physical capture endpoint.
	 *
	 * @param sourceId endpoint id from availableInputSources()
	 */
	void setInputAssignment(const QString& sourceId);

	//! @returns all inputs the active backend can offer this track.
	auto availableInputSources() const -> std::vector<AudioInputSource>;

	//! @returns the resolved source, or std::nullopt if it is unavailable.
	auto resolvedInputSource() const -> std::optional<AudioInputSource>;

	//! @returns an empty string when the saved assignment is usable.
	auto inputAssignmentStatus() const -> QString;

	bool isInputMonitoring() const { return m_inputMonitoring.load(std::memory_order_relaxed); }
	void setInputMonitoring(bool enabled) { m_inputMonitoring.store(enabled, std::memory_order_relaxed); }
	void updateInputLevel(float level);
	float takeInputLevel() { return m_inputLevel.exchange(0.f, std::memory_order_relaxed); }

	//! @returns the current peak of this track's assigned capture source.
	float inputPeak() const;
	void updateRecordingClipLength();

	inline IntModel * mixerChannelModel()
	{
		return &m_mixerChannelModel;
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
	void inputAssignmentChanged();

public slots:
	void updateClips();
	void setPlayingClips( bool isPlaying );
	void updateMixerChannel();

private:
	FloatModel m_volumeModel;
	FloatModel m_panningModel;
	IntModel m_mixerChannelModel;
	AudioBusHandle m_audioBusHandle;
	bool m_isPlaying;
	std::atomic_bool m_recording{false};
	std::atomic_int m_recordChannel{0};
	QString m_inputSourceId = QStringLiteral("system-default");
	std::atomic_bool m_inputMonitoring{false};
	std::atomic<float> m_inputLevel{0.f};
	std::shared_ptr<SampleRecordSession> m_recordingSession;
	QPointer<SampleClip> m_recordingClip;
	quint64 m_trackInputId = 0;



	friend class gui::SampleTrackView;
	friend class gui::SampleTrackWindow;

} ;


} // namespace mxm

#endif // MXM_SAMPLE_TRACK_H
