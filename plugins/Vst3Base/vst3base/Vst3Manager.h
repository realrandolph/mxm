/*
 * Vst3Manager.h - VST3 module discovery and caching
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

#ifndef MXM_VST3_MANAGER_H
#define MXM_VST3_MANAGER_H

#include <memory>
#include <string>
#include <vector>

#include <QString>

#include "MxmBridge.h"

namespace mxm
{

//! Discovers VST3 modules installed on the system and caches their class info.
class Vst3Manager
{
public:
	struct Descriptor
	{
		std::string modulePath;  //!< absolute path to the .vst3 bundle
		std::string cid;         //!< class ID (UID) of the audio module class
		QString name;
		QString vendor;
		QString version;
		QString subCategories;
		bool isInstrument = false;
	};

	//! Returns the process-wide singleton.
	static Vst3Manager& instance();

	//! (Re)discover all installed VST3 modules.
	void discover();

	//! Discovered descriptors (instrument + effect classes).
	const std::vector<Descriptor>& descriptors() const { return m_descriptors; }

	//! Create a fresh adapter instance for the given descriptor.
	std::unique_ptr<bridge::IPlugin> createPlugin(const Descriptor& desc) const;

private:
	Vst3Manager() = default;

	void discoverPath(const std::string& path);

	std::vector<Descriptor> m_descriptors;
	bool m_discovered = false;
};

} // namespace mxm

#endif // MXM_VST3_MANAGER_H
