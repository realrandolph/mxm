/*
 * Vst3EffectControls.h - controls for VST3 effect plugins
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

#ifndef MXM_VST3_EFFECT_CONTROLS_H
#define MXM_VST3_EFFECT_CONTROLS_H

#include "EffectControls.h"

namespace mxm
{

class Vst3Effect;

class Vst3EffectControls : public EffectControls
{
public:
	explicit Vst3EffectControls(Vst3Effect* effect);
	~Vst3EffectControls() override;

	int controlCount() override;
	gui::EffectControlDialog* createView() override;

	void saveSettings(QDomDocument& doc, QDomElement& that) override;
	void loadSettings(const QDomElement& that) override;
	QString nodeName() const override;

	Vst3Effect* vst3Effect() { return m_effect; }

private:
	Vst3Effect* m_effect;
};

} // namespace mxm

#endif // MXM_VST3_EFFECT_CONTROLS_H
