/*
 * Vst3EffectControlDialog.h - control dialog for VST3 effect plugins
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

#ifndef MXM_VST3_EFFECT_CONTROL_DIALOG_H
#define MXM_VST3_EFFECT_CONTROL_DIALOG_H

#include "EffectControlDialog.h"

class QPushButton;

namespace mxm
{

class Vst3EffectControls;

namespace gui
{

class MxmPluginEditor;

class Vst3EffectControlDialog : public EffectControlDialog
{
	Q_OBJECT
public:
	explicit Vst3EffectControlDialog(Vst3EffectControls* controls);
	~Vst3EffectControlDialog() override;

private:
	void togglePluginUI();

	Vst3EffectControls* m_controls;
	QPushButton* m_toggleGuiButton = nullptr;
	MxmPluginEditor* m_editor = nullptr;
};

} // namespace gui
} // namespace mxm

#endif // MXM_VST3_EFFECT_CONTROL_DIALOG_H
