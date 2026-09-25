/*
 * Vst3Effect.h - class for handling native VST3 effect plugins
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

#ifndef MXM_VST3_EFFECT_H
#define MXM_VST3_EFFECT_H

#include <memory>

#include "Effect.h"
#include "MxmPluginBridge.h"

namespace mxm
{

class Vst3EffectControls;

class Vst3Effect : public Effect
{
public:
	Vst3Effect(Model* parent, const Descriptor::SubPluginFeatures::Key* key);
	~Vst3Effect() override;

	EffectControls* controls() override;

	std::unique_ptr<MxmPluginBridge>& bridge() { return m_bridge; }

protected:
	ProcessStatus processImpl(SampleFrame* buf, const f_cnt_t frames) override;

private:
	std::unique_ptr<MxmPluginBridge> m_bridge;
	Vst3EffectControls* m_controls = nullptr;
};

} // namespace mxm

#endif // MXM_VST3_EFFECT_H
