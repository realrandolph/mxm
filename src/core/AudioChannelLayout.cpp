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

#include <QStringList>

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

struct ChannelRoute
{
	ch_cnt_t src = 0;
	ch_cnt_t dst = 0;
	float weight = 0.f;
};

constexpr size_t MaxRoutes = 128;

struct ChannelRoutes
{
	std::array<ChannelRoute, MaxRoutes> routes{};
	size_t count = 0;

	void push_back(ChannelRoute r)
	{
		if (count < MaxRoutes)
		{
			routes[count++] = r;
		}
	}

	auto begin() const noexcept { return routes.data(); }
	auto end() const noexcept { return routes.data() + count; }
	auto size() const noexcept -> size_t { return count; }
};

auto buildChannelRoutes(const AudioChannelLayout& srcLayout, const AudioChannelLayout& dstLayout)
	-> ChannelRoutes
{
	ChannelRoutes routes;
	const auto srcChannels = srcLayout.channels();
	const auto dstChannels = dstLayout.channels();

	if (srcChannels == 0 || dstChannels == 0)
	{
		return routes;
	}

	// 1. Same layout: direct 1:1 mapping by index
	if (srcLayout == dstLayout)
	{
		for (ch_cnt_t c = 0; c < srcChannels; ++c)
		{
			routes.push_back({c, c, 1.0f});
		}
		return routes;
	}

	auto findDstRole = [&](SpeakerRole role) -> int {
		for (ch_cnt_t d = 0; d < dstChannels; ++d)
		{
			if (dstLayout.role(d) == role)
			{
				return static_cast<int>(d);
			}
		}
		return -1;
	};

	const auto dstFl = findDstRole(SpeakerRole::FrontLeft);
	const auto dstFr = findDstRole(SpeakerRole::FrontRight);
	const auto dstFc = findDstRole(SpeakerRole::FrontCenter);
	const auto dstLfe = findDstRole(SpeakerRole::Lfe);

	// 2. Destination is mono (1 channel): sum source using downmixFrameToStereo then average L/R
	if (dstChannels == 1)
	{
		if (srcChannels == 1)
		{
			routes.push_back({0, 0, 1.0f});
			return routes;
		}

		for (ch_cnt_t s = 0; s < srcChannels; ++s)
		{
			const auto role = srcLayout.role(s);
			const auto gains = (role == SpeakerRole::Unknown)
				? gainsForDiscreteChannel(s)
				: gainsForRole(role);
			const auto weight = 0.5f * (gains.left + gains.right);
			if (weight > 0.0f)
			{
				routes.push_back({s, 0, weight});
			}
		}
		return routes;
	}

	// 3. Destination is stereo (2 known channels): use downmixFrameToStereo (documented gains)
	if (dstChannels == 2 && dstLayout.hasKnownRoles())
	{
		if (srcChannels == 1)
		{
			routes.push_back({0, 0, 1.0f});
			routes.push_back({0, 1, 1.0f});
			return routes;
		}

		for (ch_cnt_t s = 0; s < srcChannels; ++s)
		{
			const auto role = srcLayout.role(s);
			const auto gains = (role == SpeakerRole::Unknown)
				? gainsForDiscreteChannel(s)
				: gainsForRole(role);
			if (gains.left > 0.0f)
			{
				routes.push_back({s, 0, gains.left});
			}
			if (gains.right > 0.0f)
			{
				routes.push_back({s, 1, gains.right});
			}
		}
		return routes;
	}

	// 4. Source is mono to multi: put mono into dst FC if present, otherwise FL and FR (or channel 0 if only one)
	if (srcChannels == 1)
	{
		if (dstFc >= 0)
		{
			routes.push_back({0, static_cast<ch_cnt_t>(dstFc), 1.0f});
		}
		else if (dstFl >= 0 && dstFr >= 0)
		{
			routes.push_back({0, static_cast<ch_cnt_t>(dstFl), 1.0f});
			routes.push_back({0, static_cast<ch_cnt_t>(dstFr), 1.0f});
		}
		else if (dstChannels >= 2)
		{
			routes.push_back({0, 0, 1.0f});
			routes.push_back({0, 1, 1.0f});
		}
		else
		{
			routes.push_back({0, 0, 1.0f});
		}
		return routes;
	}

	// 5. Source is stereo to surround: FL/FR map to FL/FR; if dst lacks those roles, map index 0/1
	if (srcLayout == AudioChannelLayout::stereo() || (srcChannels == 2 && srcLayout.hasKnownRoles()))
	{
		if (dstFl >= 0 && dstFr >= 0)
		{
			routes.push_back({0, static_cast<ch_cnt_t>(dstFl), 1.0f});
			routes.push_back({1, static_cast<ch_cnt_t>(dstFr), 1.0f});
		}
		else
		{
			routes.push_back({0, 0, 1.0f});
			if (dstChannels > 1)
			{
				routes.push_back({1, 1, 1.0f});
			}
		}
		return routes;
	}

	// 6. Both layouts have known roles: match channels by SpeakerRole
	if (srcLayout.hasKnownRoles() && dstLayout.hasKnownRoles())
	{
		for (ch_cnt_t s = 0; s < srcChannels; ++s)
		{
			const auto role = srcLayout.role(s);
			if (role == SpeakerRole::Lfe)
			{
				if (dstLfe >= 0)
				{
					routes.push_back({s, static_cast<ch_cnt_t>(dstLfe), 1.0f});
				}
				else
				{
					// Fold LFE at -6 dB into FL/FR
					if (dstFl >= 0 && dstFr >= 0)
					{
						routes.push_back({s, static_cast<ch_cnt_t>(dstFl), LfeGain});
						routes.push_back({s, static_cast<ch_cnt_t>(dstFr), LfeGain});
					}
					else if (dstChannels >= 2)
					{
						routes.push_back({s, 0, LfeGain});
						routes.push_back({s, 1, LfeGain});
					}
					else
					{
						routes.push_back({s, 0, LfeGain});
					}
				}
				continue;
			}

			const auto dstCh = findDstRole(role);
			if (dstCh >= 0)
			{
				routes.push_back({s, static_cast<ch_cnt_t>(dstCh), 1.0f});
			}
			else
			{
				// Source channels whose role is absent in dst are folded by the existing
				// downmixFrameToStereo gains into FL/FR if dst has FL/FR
				const auto gains = gainsForRole(role);
				if (dstFl >= 0 && dstFr >= 0)
				{
					if (gains.left > 0.0f)
					{
						routes.push_back({s, static_cast<ch_cnt_t>(dstFl), gains.left});
					}
					if (gains.right > 0.0f)
					{
						routes.push_back({s, static_cast<ch_cnt_t>(dstFr), gains.right});
					}
				}
				else if (dstChannels >= 2)
				{
					if (gains.left > 0.0f)
					{
						routes.push_back({s, 0, gains.left});
					}
					if (gains.right > 0.0f)
					{
						routes.push_back({s, 1, gains.right});
					}
				}
				else
				{
					const auto avg = 0.5f * (gains.left + gains.right);
					routes.push_back({s, 0, avg > 0.0f ? avg : CenterGain});
				}
			}
		}
		return routes;
	}

	// 7. Discrete layouts or mixed known/unknown layouts:
	// Map by index for overlapping channels; if dst has fewer channels, fold the remaining
	// src channels into dst channels alternating so signal is not silently discarded.
	for (ch_cnt_t s = 0; s < srcChannels; ++s)
	{
		if (s < dstChannels)
		{
			routes.push_back({s, s, 1.0f});
		}
		else
		{
			const auto dstCh = static_cast<ch_cnt_t>(s % dstChannels);
			routes.push_back({s, dstCh, CenterGain});
		}
	}
	return routes;
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
	case 4:
		roles[0] = SpeakerRole::FrontLeft;
		roles[1] = SpeakerRole::FrontRight;
		roles[2] = SpeakerRole::BackLeft;
		roles[3] = SpeakerRole::BackRight;
		break;
	case 5:
		roles[0] = SpeakerRole::FrontLeft;
		roles[1] = SpeakerRole::FrontRight;
		roles[2] = SpeakerRole::FrontCenter;
		roles[3] = SpeakerRole::BackLeft;
		roles[4] = SpeakerRole::BackRight;
		break;
	case 6:
		roles[0] = SpeakerRole::FrontLeft;
		roles[1] = SpeakerRole::FrontRight;
		roles[2] = SpeakerRole::FrontCenter;
		roles[3] = SpeakerRole::Lfe;
		roles[4] = SpeakerRole::BackLeft;
		roles[5] = SpeakerRole::BackRight;
		break;
	case 7:
		roles[0] = SpeakerRole::FrontLeft;
		roles[1] = SpeakerRole::FrontRight;
		roles[2] = SpeakerRole::FrontCenter;
		roles[3] = SpeakerRole::Lfe;
		roles[4] = SpeakerRole::BackLeft;
		roles[5] = SpeakerRole::BackRight;
		roles[6] = SpeakerRole::BackCenter;
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
	case 10:
		roles[0] = SpeakerRole::FrontLeft;
		roles[1] = SpeakerRole::FrontRight;
		roles[2] = SpeakerRole::FrontCenter;
		roles[3] = SpeakerRole::Lfe;
		roles[4] = SpeakerRole::BackLeft;
		roles[5] = SpeakerRole::BackRight;
		roles[6] = SpeakerRole::SideLeft;
		roles[7] = SpeakerRole::SideRight;
		roles[8] = SpeakerRole::TopFrontLeft;
		roles[9] = SpeakerRole::TopFrontRight;
		break;
	default:
		break;
	}
	return roles;
}

auto AudioChannelLayout::presets() -> const std::vector<LayoutPreset>&
{
	static const auto s_presets = std::vector<LayoutPreset>{
		{QStringLiteral("Mono"), mono()},
		{QStringLiteral("Stereo"), stereo()},
		{QStringLiteral("3.0"), standard(3)},
		{QStringLiteral("4.0 (Quad)"), quadraphonic()},
		{QStringLiteral("5.0"), surround50()},
		{QStringLiteral("5.1"), surround51()},
		{QStringLiteral("6.1"), surround61()},
		{QStringLiteral("7.1"), surround71()},
		{QStringLiteral("7.1.2"), surround712()},
	};
	return s_presets;
}

auto AudioChannelLayout::fromPresetIndex(int index) -> AudioChannelLayout
{
	const auto& list = presets();
	if (index >= 0 && static_cast<size_t>(index) < list.size())
	{
		return list[static_cast<size_t>(index)].layout;
	}
	return AudioChannelLayout{};
}

auto AudioChannelLayout::presetIndex(const AudioChannelLayout& layout) -> int
{
	const auto& list = presets();
	for (size_t index = 0; index < list.size(); ++index)
	{
		if (list[index].layout == layout)
		{
			return static_cast<int>(index);
		}
	}
	return -1;
}

auto AudioChannelLayout::fromString(const QString& text, ch_cnt_t fallbackChannels) -> AudioChannelLayout
{
	const auto value = text.trimmed().toLower();
	if (value.isEmpty()) { return standard(fallbackChannels); }
	if (value == "mono") { return mono(); }
	if (value == "stereo") { return stereo(); }
	if (value == "3.0") { return standard(3); }
	if (value == "4.0" || value == "quadraphonic" || value == "quad") { return quadraphonic(); }
	if (value == "5.0") { return surround50(); }
	if (value == "5.1") { return surround51(); }
	if (value == "6.1") { return surround61(); }
	if (value == "7.1") { return surround71(); }
	if (value == "7.1.2") { return surround712(); }

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
	if (*this == quadraphonic()) { return "4.0"; }
	if (*this == surround50()) { return "5.0"; }
	if (*this == surround51()) { return "5.1"; }
	if (*this == surround61()) { return "6.1"; }
	if (*this == surround71()) { return "7.1"; }
	if (*this == surround712()) { return "7.1.2"; }

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
	const auto index = presetIndex(*this);
	if (index >= 0)
	{
		return presets()[static_cast<size_t>(index)].name;
	}
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
		if (channels == 1)
		{
			left = right = src.bufferPtr(0)[frame];
			auto* out = dst.framePtr(frame);
			out[0] = left;
			out[1] = right;
			continue;
		}
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

void addFrameConverted(const float* src, const AudioChannelLayout& srcLayout,
	float* dst, const AudioChannelLayout& dstLayout, float gain)
{
	if (gain == 0.0f || srcLayout.channels() == 0 || dstLayout.channels() == 0)
	{
		return;
	}

	const auto routes = buildChannelRoutes(srcLayout, dstLayout);
	for (const auto& route : routes)
	{
		dst[route.dst] += src[route.src] * (route.weight * gain);
	}
}

void addPlanarConverted(PlanarBufferView<const float> src,
	const AudioChannelLayout& srcLayout, PlanarBufferView<float> dst,
	const AudioChannelLayout& dstLayout, float gain)
{
	assert(src.channels() == srcLayout.channels());
	assert(dst.channels() == dstLayout.channels());

	const auto frames = std::min(src.frames(), dst.frames());
	if (frames == 0 || gain == 0.0f || srcLayout.channels() == 0 || dstLayout.channels() == 0)
	{
		return;
	}

	// Fast path for same layout
	if (srcLayout == dstLayout)
	{
		const auto channels = srcLayout.channels();
		for (ch_cnt_t c = 0; c < channels; ++c)
		{
			const float* in = src.bufferPtr(c);
			float* out = dst.bufferPtr(c);
			for (f_cnt_t f = 0; f < frames; ++f)
			{
				out[f] += in[f] * gain;
			}
		}
		return;
	}

	const auto routes = buildChannelRoutes(srcLayout, dstLayout);
	for (const auto& route : routes)
	{
		const float w = route.weight * gain;
		const float* in = src.bufferPtr(route.src);
		float* out = dst.bufferPtr(route.dst);
		for (f_cnt_t f = 0; f < frames; ++f)
		{
			out[f] += in[f] * w;
		}
	}
}

} // namespace mxm
