/*
 * Vst3SubPluginFeatures.cpp - plugin browser integration for VST3 plugins
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

#include "Vst3SubPluginFeatures.h"

#include <QString>

namespace mxm
{

Vst3SubPluginFeatures::Vst3SubPluginFeatures(Plugin::Type type)
	: Plugin::Descriptor::SubPluginFeatures(type)
{
}

void Vst3SubPluginFeatures::fillDescriptionWidget(QWidget* parent, const Key* k) const
{
	Q_UNUSED(parent)
	Q_UNUSED(k)
}

QString Vst3SubPluginFeatures::additionalFileExtensions(const Key& k) const
{
	Q_UNUSED(k)
	return QString();
}

QString Vst3SubPluginFeatures::displayName(const Key& k) const
{
	if (const auto* desc = getDescriptor(k))
	{
		return desc->name;
	}
	return k.name;
}

QString Vst3SubPluginFeatures::description(const Key& k) const
{
	if (const auto* desc = getDescriptor(k))
	{
		if (!desc->vendor.isEmpty())
		{
			return desc->name + QStringLiteral(" (") + desc->vendor + QStringLiteral(")");
		}
		return desc->name;
	}
	return k.name;
}

const PixmapLoader* Vst3SubPluginFeatures::logo(const Key& k) const
{
	return k.desc ? k.desc->logo : nullptr;
}

void Vst3SubPluginFeatures::listSubPluginKeys(const Plugin::Descriptor* desc, KeyList& kl) const
{
	Vst3Manager& manager = Vst3Manager::instance();
	manager.discover();

	for (const auto& descriptor : manager.descriptors())
	{
		if (descriptor.isInstrument != (m_type == Plugin::Type::Instrument))
		{
			continue;
		}

		Key::AttributeMap attributes;
		attributes[QStringLiteral("module")] = QString::fromStdString(descriptor.modulePath);
		attributes[QStringLiteral("cid")] = QString::fromStdString(descriptor.cid);

		kl.push_back(Key(desc, descriptor.name, attributes));
	}
}

const Vst3Manager::Descriptor* Vst3SubPluginFeatures::getDescriptor(const Key& k)
{
	const QString module = k.attributes.value(QStringLiteral("module"));
	const QString cid = k.attributes.value(QStringLiteral("cid"));

	Vst3Manager& manager = Vst3Manager::instance();
	manager.discover();

	for (const auto& descriptor : manager.descriptors())
	{
		if (QString::fromStdString(descriptor.modulePath) == module
			&& QString::fromStdString(descriptor.cid) == cid)
		{
			return &descriptor;
		}
	}
	return nullptr;
}

} // namespace mxm
