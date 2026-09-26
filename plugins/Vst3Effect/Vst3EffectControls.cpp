/*
 * Vst3EffectControls.cpp - controls for VST3 effect plugins
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

#include "Vst3EffectControls.h"

#include "Vst3Effect.h"
#include "Vst3EffectControlDialog.h"

#include "MxmPluginBridge.h"

namespace mxm
{

Vst3EffectControls::Vst3EffectControls(Vst3Effect* effect)
	: EffectControls(effect)
	, m_effect(effect)
{
}

Vst3EffectControls::~Vst3EffectControls() = default;

int Vst3EffectControls::controlCount()
{
	return m_effect->bridge() ? 1 : 0;
}

gui::EffectControlDialog* Vst3EffectControls::createView()
{
	return new gui::Vst3EffectControlDialog(this);
}

void Vst3EffectControls::saveSettings(QDomDocument& doc, QDomElement& that)
{
	if (m_effect->bridge())
	{
		m_effect->bridge()->saveSettings(doc, that);
	}
}

void Vst3EffectControls::loadSettings(const QDomElement& that)
{
	if (m_effect->bridge())
	{
		m_effect->bridge()->loadSettings(that);
	}
}

QString Vst3EffectControls::nodeName() const
{
	return "vst3effectcontrols";
}

} // namespace mxm
