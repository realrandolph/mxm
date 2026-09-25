/*
 * Vst3Effect.cpp - class for handling native VST3 effect plugins
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

#include "Vst3Effect.h"

#include <array>
#include <cstring>

#include "Vst3EffectControls.h"
#include "Vst3Manager.h"
#include "Vst3SubPluginFeatures.h"

#include "SampleFrame.h"

#include "embed.h"
#include "plugin_export.h"

namespace mxm
{

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT vst3effect_plugin_descriptor =
{
	MXM_STRINGIFY(PLUGIN_NAME),
	"VST3",
	QT_TRANSLATE_NOOP("PluginBrowser", "plugin for using native VST3 effects inside MXM."),
	"Randolph Nimmer <dolf/at/dolfsdomain.com>",
	0x0100,
	Plugin::Type::Effect,
	new PluginPixmapLoader("logo"),
	nullptr,
	new Vst3SubPluginFeatures(Plugin::Type::Effect)
};

} // extern "C"


Vst3Effect::Vst3Effect(Model* parent, const Descriptor::SubPluginFeatures::Key* key)
	: Effect(&vst3effect_plugin_descriptor, parent, key)
{
	bool loaded = false;

	const auto* descriptor = Vst3SubPluginFeatures::getDescriptor(*key);
	if (descriptor)
	{
		auto plugin = Vst3Manager::instance().createPlugin(*descriptor);
		if (plugin)
		{
			m_bridge = std::make_unique<MxmPluginBridge>(this, std::move(plugin));
			m_bridge->init();
			loaded = m_bridge->isValid();
			setDisplayName(descriptor->name);
		}
	}

	if (!loaded)
	{
		collectErrorForUI(tr("The VST3 plugin could not be loaded."));
	}
	setDontRun(!loaded);
}

Vst3Effect::~Vst3Effect() = default;

EffectControls* Vst3Effect::controls()
{
	if (!m_controls)
	{
		m_controls = new Vst3EffectControls(this);
	}
	return m_controls;
}

Effect::ProcessStatus Vst3Effect::processImpl(SampleFrame* buf, const f_cnt_t frames)
{
	if (!m_bridge || !m_bridge->isValid())
	{
		return ProcessStatus::ContinueIfNotQuiet;
	}

	static thread_local std::array<SampleFrame, MAXIMUM_BUFFER_SIZE> tempBuf;

	m_bridge->copyBuffersFromLmms(buf, frames);
	m_bridge->run(frames);
	m_bridge->copyBuffersToLmms(tempBuf.data(), frames);

	const float w = wetLevel();
	const float d = dryLevel();
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		buf[f][0] = w * tempBuf[f][0] + d * buf[f][0];
		buf[f][1] = w * tempBuf[f][1] + d * buf[f][1];
	}

	return ProcessStatus::ContinueIfNotQuiet;
}

extern "C"
{

PLUGIN_EXPORT Plugin* mxm_plugin_main(Model* parent, void* data)
{
	return new Vst3Effect(parent,
		static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data));
}

} // extern "C"

} // namespace mxm
