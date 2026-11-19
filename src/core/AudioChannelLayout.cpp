/*
 * AudioChannelLayout.cpp - channel counts, speaker roles and channel mapping
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

#include "AudioChannelLayout.h"

#include <algorithm>
#include <cmath>

namespace mxm
{

namespace
{

constexpr auto CenterGain = 0.70710678f; // -3 dB
constexpr auto LfeGain = 0.5f;           // -6 dB
constexpr auto SurroundGain = 0.70710678f;

struct StereoGains
{
	float left = 0.f;
	float right = 0.f;
};

auto gainsForRole(SpeakerRole role) -> StereoGains
{
	switch (role)
	{
	case SpeakerRole::FrontLeft:
		return {1.f, 0.f};
	case SpeakerRole::FrontRight:
		return {0.f, 1.f};
	case SpeakerRole::SideLeft:
	case SpeakerRole::BackLeft:
		return {SurroundGain, 0.f};
	case SpeakerRole::SideRight:
	case SpeakerRole::BackRight:
		return {0.f, SurroundGain};
	case SpeakerRole::FrontCenter:
	case SpeakerRole::FrontLeftOfCenter:
	case SpeakerRole::FrontRightOfCenter:
		return {CenterGain, CenterGain};
	case SpeakerRole::Lfe:
		return {LfeGain, LfeGain};
	case SpeakerRole::BackCenter:
	case SpeakerRole::TopFrontLeft:
	case SpeakerRole::TopFrontRight:
	case SpeakerRole::TopBackLeft:
	case SpeakerRole::TopBackRight:
		return {SurroundGain, SurroundGain};
	case SpeakerRole::Unknown:
		return {0.f, 0.f};
	}
	return {0.f, 0.f};
}

//! Discrete layouts preserve channel order but have no speaker roles. The first
//! two channels are mapped left/right and the rest alternate at -3 dB so no
//! channel is silently dropped.
auto gainsForDiscreteChannel(ch_cnt_t channel) -> StereoGains
{
	switch (channel)
	{
	case 0:
		return {1.f, 0.f};
	case 1:
		return {0.f, 1.f};
	default:
		return (channel % 2 == 0) ? StereoGains{CenterGain, 0.f} : StereoGains{0.f, CenterGain};
	}
}

} // namespace

AudioChannelLayout::AudioChannelLayout(ch_cnt_t channels)
	: m_channels{std::min(channels, MaxChannelsPerLayout)}
{
}

AudioChannelLayout::AudioChannelLayout(ch_cnt_t channels, Roles roles)
	: m_channels{std::min(channels, MaxChannelsPerLayout)}
	, m_roles{roles}
{
	for (ch_cnt_t channel = m_channels; channel < MaxChannelsPerLayout; ++channel)
	{
		m_roles[channel] = SpeakerRole::Unknown;
	}
}

auto AudioChannelLayout::rolesFor(ch_cnt_t channels) -> Roles
{
	Roles roles{};
	switch (channels)
	{
	case 1:
		roles[0] = SpeakerRole::FrontCenter;
		break;
	case 2:
		roles[0] = SpeakerRole::FrontLeft;
		roles[1] = SpeakerRole::FrontRight;
		break;
	case 3:
		roles[0] = SpeakerRole::FrontLeft;
		roles[1] = SpeakerRole::FrontRight;
		roles[2] = SpeakerRole::FrontCenter;
		break;
	case 6:
		roles[0] = SpeakerRole::FrontLeft;
		roles[1] = SpeakerRole::FrontRight;
		roles[2] = SpeakerRole::FrontCenter;
		roles[3] = SpeakerRole::Lfe;
		roles[4] = SpeakerRole::BackLeft;
		roles[5] = SpeakerRole::BackRight;
		break;
	case 8:
		roles[0] = SpeakerRole::FrontLeft;
		roles[1] = SpeakerRole::FrontRight;
		roles[2] = SpeakerRole::FrontCenter;
		roles[3] = SpeakerRole::Lfe;
		roles[4] = SpeakerRole::BackLeft;
		roles[5] = SpeakerRole::BackRight;
		roles[6] = SpeakerRole::SideLeft;
		roles[7] = SpeakerRole::SideRight;
		break;
	default:
		break;
	}
	return roles;
}

auto AudioChannelLayout::fromString(const QString& text, ch_cnt_t fallbackChannels) -> AudioChannelLayout
{
	const auto value = text.trimmed().toLower();
	if (value.isEmpty()) { return standard(fallbackChannels); }
	if (value == "mono") { return mono(); }
	if (value == "stereo") { return stereo(); }
	if (value == "5.1") { return surround51(); }
	if (value == "7.1") { return surround71(); }

	if (value.startsWith("discrete:"))
	{
		bool ok = false;
		const auto channels = value.mid(9).toUInt(&ok);
		if (ok) { return discrete(static_cast<ch_cnt_t>(std::min<std::uint64_t>(channels, MaxChannelsPerLayout))); }
	}

	if (value.startsWith("roles:"))
	{
		const auto names = value.mid(6).split(',', Qt::SkipEmptyParts);
		if (!names.isEmpty())
		{
			Roles roles{};
			ch_cnt_t channel = 0;
			for (const auto& name : names)
			{
				if (channel >= MaxChannelsPerLayout) { break; }
				roles[channel++] = speakerRoleFromName(name);
			}
			return AudioChannelLayout{channel, roles};
		}
	}

	return discrete(fallbackChannels);
}

auto AudioChannelLayout::toString() const -> QString
{
	if (m_channels == 0) { return "discrete:0"; }
	if (*this == mono()) { return "mono"; }
	if (*this == stereo()) { return "stereo"; }
	if (*this == surround51()) { return "5.1"; }
	if (*this == surround71()) { return "7.1"; }

	if (!hasKnownRoles()) { return QStringLiteral("discrete:%1").arg(m_channels); }

	QStringList names;
	for (ch_cnt_t channel = 0; channel < m_channels; ++channel)
	{
		names.append(QString::fromLatin1(speakerRoleName(m_roles[channel])));
	}
	return QStringLiteral("roles:") + names.join(',');
}

auto AudioChannelLayout::displayName() const -> QString
{
	if (m_channels == 0) { return QStringLiteral("Empty"); }
	if (*this == mono()) { return QStringLiteral("Mono"); }
	if (*this == stereo()) { return QStringLiteral("Stereo"); }
	if (*this == surround51()) { return QStringLiteral("5.1 surround"); }
	if (*this == surround71()) { return QStringLiteral("7.1 surround"); }
	if (!hasKnownRoles()) { return QStringLiteral("%1-channel discrete").arg(m_channels); }
	return QStringLiteral("%1-channel (%2)").arg(m_channels).arg(toString());
}

auto AudioChannelLayout::role(ch_cnt_t channel) const -> SpeakerRole
{
	return channel < m_channels ? m_roles[channel] : SpeakerRole::Unknown;
}

auto AudioChannelLayout::hasKnownRoles() const -> bool
{
	for (ch_cnt_t channel = 0; channel < m_channels; ++channel)
	{
		if (m_roles[channel] == SpeakerRole::Unknown) { return false; }
	}
	return m_channels > 0;
}

auto AudioChannelLayout::operator==(const AudioChannelLayout& other) const -> bool
{
	return m_channels == other.m_channels && m_roles == other.m_roles;
}

auto speakerRoleName(SpeakerRole role) -> const char*
{
	switch (role)
	{
	case SpeakerRole::FrontLeft: return "FL";
	case SpeakerRole::FrontRight: return "FR";
	case SpeakerRole::FrontCenter: return "FC";
	case SpeakerRole::Lfe: return "LFE";
	case SpeakerRole::BackLeft: return "BL";
	case SpeakerRole::BackRight: return "BR";
	case SpeakerRole::SideLeft: return "SL";
	case SpeakerRole::SideRight: return "SR";
	case SpeakerRole::FrontLeftOfCenter: return "FLC";
	case SpeakerRole::FrontRightOfCenter: return "FRC";
	case SpeakerRole::BackCenter: return "BC";
	case SpeakerRole::TopFrontLeft: return "TFL";
	case SpeakerRole::TopFrontRight: return "TFR";
	case SpeakerRole::TopBackLeft: return "TBL";
	case SpeakerRole::TopBackRight: return "TBR";
	case SpeakerRole::Unknown: return "?";
	}
	return "?";
}

auto speakerRoleFromName(const QString& name) -> SpeakerRole
{
	const auto value = name.trimmed().toUpper();
	if (value == "FL") { return SpeakerRole::FrontLeft; }
	if (value == "FR") { return SpeakerRole::FrontRight; }
	if (value == "FC") { return SpeakerRole::FrontCenter; }
	if (value == "LFE") { return SpeakerRole::Lfe; }
	if (value == "BL") { return SpeakerRole::BackLeft; }
	if (value == "BR") { return SpeakerRole::BackRight; }
	if (value == "SL") { return SpeakerRole::SideLeft; }
	if (value == "SR") { return SpeakerRole::SideRight; }
	if (value == "FLC") { return SpeakerRole::FrontLeftOfCenter; }
	if (value == "FRC") { return SpeakerRole::FrontRightOfCenter; }
	if (value == "BC") { return SpeakerRole::BackCenter; }
	if (value == "TFL") { return SpeakerRole::TopFrontLeft; }
	if (value == "TFR") { return SpeakerRole::TopFrontRight; }
	if (value == "TBL") { return SpeakerRole::TopBackLeft; }
	if (value == "TBR") { return SpeakerRole::TopBackRight; }
	return SpeakerRole::Unknown;
}

void downmixFrameToStereo(
	const float* src, const AudioChannelLayout& layout, float& dstLeft, float& dstRight)
{
	dstLeft = 0.f;
	dstRight = 0.f;

	const auto channels = layout.channels();
	if (channels == 1)
	{
		// A single channel is a mono signal, not a centre-only surround channel.
		dstLeft = dstRight = src[0];
		return;
	}
	for (ch_cnt_t channel = 0; channel < channels; ++channel)
	{
		const auto role = layout.role(channel);
		const auto gains = role == SpeakerRole::Unknown
			? gainsForDiscreteChannel(channel)
			: gainsForRole(role);
		dstLeft += src[channel] * gains.left;
		dstRight += src[channel] * gains.right;
	}
}

void downmixToStereo(
	PlanarBufferView<const float> src, const AudioChannelLayout& layout, InterleavedBufferView<float, 2> dst)
{
	assert(src.channels() == layout.channels());
	const auto frames = std::min(src.frames(), dst.frames());
	for (f_cnt_t frame = 0; frame < frames; ++frame)
	{
		float left = 0.f;
		float right = 0.f;
		const auto channels = layout.channels();
		for (ch_cnt_t channel = 0; channel < channels; ++channel)
		{
			const auto role = layout.role(channel);
			const auto gains = role == SpeakerRole::Unknown
				? gainsForDiscreteChannel(channel)
				: gainsForRole(role);
			left += src.bufferPtr(channel)[frame] * gains.left;
			right += src.bufferPtr(channel)[frame] * gains.right;
		}
		auto* out = dst.framePtr(frame);
		out[0] = left;
		out[1] = right;
	}
}

void downmixToStereo(
	InterleavedBufferView<const float> src, const AudioChannelLayout& layout, PlanarBufferView<float, 2> dst)
{
	assert(src.channels() == layout.channels());
	const auto frames = std::min(src.frames(), dst.frames());
	for (f_cnt_t frame = 0; frame < frames; ++frame)
	{
		float left = 0.f;
		float right = 0.f;
		downmixFrameToStereo(src.framePtr(frame), layout, left, right);
		dst.bufferPtr(0)[frame] = left;
		dst.bufferPtr(1)[frame] = right;
	}
}

} // namespace mxm
