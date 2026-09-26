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

#include <algorithm>
#include <cstdlib>

#include <QDir>
#include <QFileInfo>
#include <QStringList>

#ifdef MXM_BUILD_WIN32
#include <objbase.h>
#endif

namespace mxm
{

#ifdef MXM_BUILD_WIN32
namespace
{

class ComApartment
{
public:
	ComApartment() : m_initialized(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {}
	~ComApartment()
	{
		if (m_initialized)
		{
			CoUninitialize();
		}
	}

private:
	bool m_initialized;
};

void ensureComInitialized()
{
	static thread_local ComApartment apartment;
}

} // namespace
#endif

Vst3Manager& Vst3Manager::instance()
{
	static Vst3Manager manager;
	return manager;
}

void Vst3Manager::discover()
{
#ifdef MXM_BUILD_WIN32
	// VST3 modules may use COM while their DLL and factory are initialized.
	ensureComInitialized();
#endif

	std::lock_guard<std::mutex> lock(m_mutex);

	// Discovery is performed once; plugins do not change during a session and
	// re-discovering would invalidate descriptors handed out to other threads.
	if (m_discovered)
	{
		return;
	}

	m_descriptors.clear();

	// CI and other controlled environments can restrict discovery to VST3_PATH
	// so host-installed plugins cannot make the scan non-deterministic.
	if (!std::getenv("MXM_VST3_PATH_ONLY"))
	{
		// Standard locations (and the SDK's own application-level path).
		auto modulePaths = VST3::Hosting::Module::getModulePaths();
		for (const auto& path : modulePaths)
		{
			discoverPath(path);
		}
	}

	// VST3_PATH environment variable: extra locations (OS list separator).
	// Each entry may be either a .vst3 bundle or a directory to scan
	// recursively for .vst3 bundles.
	if (const char* extra = std::getenv("VST3_PATH"))
	{
		const QStringList paths = QString::fromLocal8Bit(extra).split(QDir::listSeparator(), Qt::SkipEmptyParts);
		for (const QString& path : paths)
		{
			discoverPathOrDirectory(path.toStdString());
		}
	}

	m_discovered = true;
}

void Vst3Manager::discoverPathOrDirectory(const std::string& path)
{
	QFileInfo info(QString::fromStdString(path));
	if (!info.isDir())
	{
		discoverPath(path);
		return;
	}

	// A .vst3 bundle is itself a directory; treat it directly.
	if (info.fileName().endsWith(QStringLiteral(".vst3")))
	{
		discoverPath(path);
		return;
	}

	// Otherwise scan for .vst3 bundles recursively.
	QDir dir(info.absoluteFilePath());
	const QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
	for (const QString& entry : entries)
	{
		discoverPathOrDirectory(dir.absoluteFilePath(entry).toStdString());
	}
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

		// Skip duplicates (the same module can appear in several locations).
		const auto duplicate = std::any_of(m_descriptors.begin(), m_descriptors.end(),
			[&](const Descriptor& d) { return d.modulePath == desc.modulePath && d.cid == desc.cid; });
		if (!duplicate)
		{
			m_descriptors.push_back(std::move(desc));
		}
	}
}

std::unique_ptr<bridge::IPlugin> Vst3Manager::createPlugin(const Descriptor& desc) const
{
#ifdef MXM_BUILD_WIN32
	ensureComInitialized();
#endif

	auto plugin = std::make_unique<Vst3Plugin>(desc.modulePath, desc.cid);
	if (!plugin->isValid())
	{
		return nullptr;
	}
	return plugin;
}

} // namespace mxm
