/*
 * Vst3Instrument.h - class for handling native VST3 instrument plugins
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

#ifndef MXM_VST3_INSTRUMENT_H
#define MXM_VST3_INSTRUMENT_H

#include <memory>

#include "Instrument.h"
#include "MxmPluginBridge.h"

namespace mxm
{

class Vst3Instrument : public Instrument
{
public:
	Vst3Instrument(InstrumentTrack* instrumentTrack, const Descriptor::SubPluginFeatures::Key* key);
	~Vst3Instrument() override;

	void play(SampleFrame* buf) override;
	bool handleMidiEvent(const MidiEvent& event, const TimePos& time, f_cnt_t offset) override;

	void saveSettings(QDomDocument& doc, QDomElement& that) override;
	void loadSettings(const QDomElement& that) override;
	QString nodeName() const override;

	gui::PluginView* instantiateView(QWidget* parent) override;

	std::unique_ptr<MxmPluginBridge>& bridge() { return m_bridge; }

private:
	std::unique_ptr<MxmPluginBridge> m_bridge;
};

} // namespace mxm

#endif // MXM_VST3_INSTRUMENT_H
