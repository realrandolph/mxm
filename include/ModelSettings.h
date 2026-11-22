/*
 * ModelSettings.h - persist a list of automatable models once for save and load
 *
 * Copyright (c) 2026 MXM developers
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

#ifndef MXM_MODEL_SETTINGS_H
#define MXM_MODEL_SETTINGS_H

#include <QDomDocument>
#include <QDomElement>

namespace mxm
{

//! @a list is invoked with a visitor `(model, name)` for each persisted model.
template<typename List>
void saveModelSettings(QDomDocument& doc, QDomElement& element, List&& list)
{
	list([&](auto& model, const char* name) {
		model.saveSettings(doc, element, name);
	});
}

template<typename List>
void loadModelSettings(const QDomElement& element, List&& list)
{
	list([&](auto& model, const char* name) {
		model.loadSettings(element, name);
	});
}

template<typename... Models>
void setLogarithmicModels(Models&... models)
{
	(models.setScaleLogarithmic(true), ...);
}

} // namespace mxm

//! Defines saveSettings/loadSettings from one `visit(model, "name");` list.
//! Invoke inside the namespace where @a Class is defined.
#define MXM_DEFINE_MODEL_SETTINGS(Class, ...) \
void Class::saveSettings(QDomDocument& doc, QDomElement& element) \
{ \
	::mxm::saveModelSettings(doc, element, [&](auto&& visit) { __VA_ARGS__ }); \
} \
void Class::loadSettings(const QDomElement& element) \
{ \
	::mxm::loadModelSettings(element, [&](auto&& visit) { __VA_ARGS__ }); \
}

#endif // MXM_MODEL_SETTINGS_H
