/*
 * Vst3Manager.cpp - VST3 module discovery and caching
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

#include "Vst3Manager.h"

#include "Vst3Plugin.h"

#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

#include <cstdlib>

#include <QDir>
#include <QStringList>

namespace mxm
{

Vst3Manager& Vst3Manager::instance()
{
	static Vst3Manager manager;
	return manager;
}

void Vst3Manager::discover()
{
	m_descriptors.clear();

	// Standard locations (and the SDK's own application-level path).
	auto modulePaths = VST3::Hosting::Module::getModulePaths();
	for (const auto& path : modulePaths)
	{
		discoverPath(path);
	}

	// VST3_PATH environment variable: colon-separated extra locations.
	if (const char* extra = std::getenv("VST3_PATH"))
	{
		const QStringList paths = QString::fromLocal8Bit(extra).split(':', Qt::SkipEmptyParts);
		for (const QString& path : paths)
		{
			discoverPath(path.toStdString());
		}
	}

	m_discovered = true;
}

void Vst3Manager::discoverPath(const std::string& path)
{
	std::string error;
	auto module = VST3::Hosting::Module::create(path, error);
	if (!module)
	{
		return;
	}

	auto factory = module->getFactory();
	for (auto& classInfo : factory.classInfos())
	{
		// Only audio module classes are hosted by MXM.
		if (classInfo.category() != kVstAudioEffectClass)
		{
			continue;
		}

		Descriptor desc;
		desc.modulePath = path;
		desc.cid = classInfo.ID().toString();
		desc.name = QString::fromStdString(classInfo.name());
		desc.vendor = QString::fromStdString(classInfo.vendor());
		desc.version = QString::fromStdString(classInfo.version());
		desc.subCategories = QString::fromStdString(classInfo.subCategoriesString());
		desc.isInstrument = desc.subCategories.startsWith(QStringLiteral("Instrument"));

		m_descriptors.push_back(std::move(desc));
	}
}

std::unique_ptr<bridge::IPlugin> Vst3Manager::createPlugin(const Descriptor& desc) const
{
	auto plugin = std::make_unique<Vst3Plugin>(desc.modulePath, desc.cid);
	if (!plugin->isValid())
	{
		return nullptr;
	}
	return plugin;
}

} // namespace mxm
