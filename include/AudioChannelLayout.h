/*
 * AudioChannelLayout.h - channel counts, speaker roles and channel mapping
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
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#ifndef MXM_AUDIO_CHANNEL_LAYOUT_H
#define MXM_AUDIO_CHANNEL_LAYOUT_H

#include <array>
#include <optional>

#include <QString>
#include <QStringList>

#include "AudioBufferView.h"
#include "MxmTypes.h"
#include "mxm_constants.h"
#include "mxm_export.h"

namespace mxm
{

/**
 * The role of a channel within a channel layout.
 *
 * `Unknown` denotes a discrete channel whose speaker role is not known. A six
 * channel recording with `Unknown` roles is a six channel recording, not
 * necessarily 5.1.
 */
enum class SpeakerRole : std::uint8_t
{
	Unknown,
	FrontLeft,
	FrontRight,
	FrontCenter,
	Lfe,
	BackLeft,
	BackRight,
	SideLeft,
	SideRight,
	FrontLeftOfCenter,
	FrontRightOfCenter,
	BackCenter,
	TopFrontLeft,
	TopFrontRight,
	TopBackLeft,
	TopBackRight,
};

//! Maximum number of channels that can carry an explicit speaker role.
inline constexpr auto MaxChannelsPerLayout = MaxChannelsPerAudioBuffer;

/**
 * A channel count paired with an optional speaker assignment.
 *
 * The channel count and the channel ordering are separate pieces of metadata.
 * For known layouts the channel order is the order of `roles()`. For discrete
 * layouts the channels retain the order in which they were captured.
 */
class MXM_EXPORT AudioChannelLayout
{
public:
	using Roles = std::array<SpeakerRole, MaxChannelsPerLayout>;

	AudioChannelLayout() = default;
	explicit AudioChannelLayout(ch_cnt_t channels);
	AudioChannelLayout(ch_cnt_t channels, Roles roles);

	static auto mono() -> AudioChannelLayout { return AudioChannelLayout{1, rolesFor(1)}; }
	static auto stereo() -> AudioChannelLayout { return AudioChannelLayout{2, rolesFor(2)}; }
	static auto surround51() -> AudioChannelLayout { return AudioChannelLayout{6, rolesFor(6)}; }
	static auto surround71() -> AudioChannelLayout { return AudioChannelLayout{8, rolesFor(8)}; }

	//! A layout with `channels` channels whose speaker roles are unknown.
	static auto discrete(ch_cnt_t channels) -> AudioChannelLayout { return AudioChannelLayout{channels}; }

	//! Standard roles for a channel count, or unknown roles if there is no standard layout.
	static auto rolesFor(ch_cnt_t channels) -> Roles;

	//! Binds a channel count to a standard layout where one is known.
	static auto standard(ch_cnt_t channels) -> AudioChannelLayout { return AudioChannelLayout{channels, rolesFor(channels)}; }

	//! Parses "mono", "stereo", "5.1", "7.1" and "discrete:N".
	static auto fromString(const QString& text, ch_cnt_t fallbackChannels = 2) -> AudioChannelLayout;

	//! Serializes to a stable, human-readable string suitable for project files.
	auto toString() const -> QString;

	//! @returns a short display name such as "Stereo" or "6-channel discrete".
	auto displayName() const -> QString;

	auto channels() const -> ch_cnt_t { return m_channels; }
	auto role(ch_cnt_t channel) const -> SpeakerRole;
	auto roles() const -> const Roles& { return m_roles; }

	//! True if every channel has a known speaker role.
	auto hasKnownRoles() const -> bool;

	//! True if the layout has no standard speaker assignment.
	auto isDiscrete() const -> bool { return !hasKnownRoles(); }

	auto operator==(const AudioChannelLayout& other) const -> bool;
	auto operator!=(const AudioChannelLayout& other) const -> bool { return !(*this == other); }

private:
	ch_cnt_t m_channels = 0;
	Roles m_roles{};
};

MXM_EXPORT auto speakerRoleName(SpeakerRole role) -> const char*;
MXM_EXPORT auto speakerRoleFromName(const QString& name) -> SpeakerRole;

/**
 * @brief Mixes an interleaved frame with the given layout down to stereo.
 *
 * Channels that have no dedicated stereo position are folded into both
 * outputs. The source frame is never modified. This is the explicit
 * compatibility mapping used by destinations that only accept stereo, such as
 * the existing stereo mixer and plugin signal paths.
 *
 * @param src pointer to `layout.channels()` interleaved samples
 * @param layout source channel layout
 * @param dstLeft receives the left output sample
 * @param dstRight receives the right output sample
 */
MXM_EXPORT void downmixFrameToStereo(
	const float* src, const AudioChannelLayout& layout, float& dstLeft, float& dstRight);

/**
 * @brief Copies a planar source block to an interleaved stereo destination.
 *
 * The channel count of @a src must match @a layout. This is used at playback
 * and monitoring boundaries that have fewer channels than the stored clip.
 */
MXM_EXPORT void downmixToStereo(
	PlanarBufferView<const float> src, const AudioChannelLayout& layout, InterleavedBufferView<float, 2> dst);

/**
 * @brief Copies an interleaved source block to a planar stereo destination.
 */
MXM_EXPORT void downmixToStereo(InterleavedBufferView<const float> src, const AudioChannelLayout& layout,
	PlanarBufferView<float, 2> dst);

} // namespace mxm

#endif // MXM_AUDIO_CHANNEL_LAYOUT_H
