/*
 * Vst3Instrument.cpp - class for handling native VST3 instrument plugins
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

#include "Vst3Instrument.h"

#include "AudioEngine.h"
#include "Engine.h"
#include "InstrumentPlayHandle.h"
#include "InstrumentTrack.h"
#include "Vst3InstrumentView.h"
#include "Vst3Manager.h"
#include "Vst3SubPluginFeatures.h"

#include "embed.h"
#include "plugin_export.h"

namespace mxm
{

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT vst3instrument_plugin_descriptor =
{
	MXM_STRINGIFY(PLUGIN_NAME),
	"VST3",
	QT_TRANSLATE_NOOP("PluginBrowser", "plugin for using native VST3 instruments inside MXM."),
	"Randolph Nimmer <dolf/at/dolfsdomain.com>",
	0x0100,
	Plugin::Type::Instrument,
	new PluginPixmapLoader("logo"),
	nullptr,
	new Vst3SubPluginFeatures(Plugin::Type::Instrument)
};

} // extern "C"


Vst3Instrument::Vst3Instrument(InstrumentTrack* instrumentTrack,
	const Descriptor::SubPluginFeatures::Key* key)
	: Instrument(instrumentTrack, &vst3instrument_plugin_descriptor, key,
		Flag::IsSingleStreamed | Flag::IsMidiBased)
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
		collectErrorForUI(tr("The VST3 instrument could not be loaded."));
	}

	auto iph = new InstrumentPlayHandle(this, instrumentTrack);
	Engine::audioEngine()->addPlayHandle(iph);
}

Vst3Instrument::~Vst3Instrument()
{
	Engine::audioEngine()->removePlayHandlesOfTypes(instrumentTrack(),
		PlayHandle::Type::NotePlayHandle | PlayHandle::Type::InstrumentPlayHandle);
}

void Vst3Instrument::play(SampleFrame* buf)
{
	if (!m_bridge || !m_bridge->isValid())
	{
		return;
	}

	const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
	m_bridge->run(frames);
	m_bridge->copyBuffersToLmms(buf, frames);
}

bool Vst3Instrument::handleMidiEvent(const MidiEvent& event, const TimePos& time, f_cnt_t offset)
{
	Q_UNUSED(time)
	if (m_bridge)
	{
		m_bridge->handleMidiEvent(event, offset);
	}
	return true;
}

void Vst3Instrument::saveSettings(QDomDocument& doc, QDomElement& that)
{
	if (m_bridge)
	{
		m_bridge->saveSettings(doc, that);
	}
}

void Vst3Instrument::loadSettings(const QDomElement& that)
{
	if (m_bridge)
	{
		m_bridge->loadSettings(that);
	}
}

QString Vst3Instrument::nodeName() const
{
	return "vst3instrument";
}

gui::PluginView* Vst3Instrument::instantiateView(QWidget* parent)
{
	return new gui::Vst3InstrumentView(this, parent);
}

extern "C"
{

PLUGIN_EXPORT Plugin* mxm_plugin_main(Model* parent, void* data)
{
	return new Vst3Instrument(static_cast<InstrumentTrack*>(parent),
		static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data));
}

} // extern "C"

} // namespace mxm
