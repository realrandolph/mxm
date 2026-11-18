/*
 * SampleRecordHandle.h - play-handle for recording a sample
 *
 * Copyright (c) 2008 Csaba Hruska <csaba.hruska/at/gmail.com>
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

#ifndef MXM_SAMPLE_RECORD_HANDLE_H
#define MXM_SAMPLE_RECORD_HANDLE_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include <QPointer>

#include "PlayHandle.h"

namespace mxm
{


class SampleBuffer;
class SampleClip;
class SampleTrack;

struct SampleRecordSession
{
	std::atomic_bool active{true};
	std::atomic_uint64_t recordedFrames{0};
};


class SampleRecordHandle : public PlayHandle
{
public:
	SampleRecordHandle(SampleClip* clip, std::shared_ptr<SampleRecordSession> session);
	~SampleRecordHandle() override;

	void play( SampleFrame* _working_buffer ) override;
	bool isFinished() const override;

	bool isFromTrack( const Track * _track ) const override;

private:
	void writeBuffer(const SampleFrame* _ab,
						const f_cnt_t _frames );

	std::vector<SampleFrame> m_recordedFrames;

	SampleTrack * m_track;
	QPointer<SampleClip> m_clip;
	std::shared_ptr<SampleRecordSession> m_session;
	int m_recordChannel;
	bool m_inputMonitoring;

} ;


} // namespace mxm

#endif // MXM_SAMPLE_RECORD_HANDLE_H
