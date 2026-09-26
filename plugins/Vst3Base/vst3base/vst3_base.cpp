/*
 * vst3_base.cpp - VST3 host base code shared by the VST3 effect/instrument
 *
 * Copyright (c) 2026 Randolph Nimmer <dolf/at/dolfsdomain.com>
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

#include "MxmCommonMacros.h"
#include "Plugin.h"
#include "vst3base_export.h"

namespace mxm
{

extern "C"
{

Plugin::Descriptor VST3BASE_EXPORT vst3base_plugin_descriptor =
{
	MXM_STRINGIFY(PLUGIN_NAME),
	"VST3 Base",
	"library for MXM's native VST3 plugin host",
	"Randolph Nimmer <dolf/at/dolfsdomain.com>",
	0x0100,
	Plugin::Type::Library,
	nullptr,
	nullptr,
	nullptr,
};

}

} // namespace mxm
