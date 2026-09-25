/*
 * Vst3SubPluginFeatures.h - plugin browser integration for VST3 plugins
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

#ifndef MXM_VST3_SUBPLUGIN_FEATURES_H
#define MXM_VST3_SUBPLUGIN_FEATURES_H

#include "Plugin.h"

#include "Vst3Manager.h"

namespace mxm
{

//! Maps discovered VST3 plugins into the MXM plugin browser / effect selector.
class Vst3SubPluginFeatures : public Plugin::Descriptor::SubPluginFeatures
{
public:
	explicit Vst3SubPluginFeatures(Plugin::Type type);

	void fillDescriptionWidget(QWidget* parent, const Key* k) const override;
	QString additionalFileExtensions(const Key& k) const override;
	QString displayName(const Key& k) const override;
	QString description(const Key& k) const override;
	const PixmapLoader* logo(const Key& k) const override;
	void listSubPluginKeys(const Plugin::Descriptor* desc, KeyList& kl) const override;

	//! Resolve a key to its discovery descriptor (or nullptr).
	static const Vst3Manager::Descriptor* getDescriptor(const Key& k);
};

} // namespace mxm

#endif // MXM_VST3_SUBPLUGIN_FEATURES_H
