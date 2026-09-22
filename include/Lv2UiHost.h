/*
 * Lv2UiHost.h - Suil based native LV2 UI host
 *
 * Copyright (c) 2026 dolf <dolfnimmer@proton.me>
 *
 * This file is part of LMMS - https://lmms.io
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

#ifndef LMMS_GUI_LV2_UI_HOST_H
#define LMMS_GUI_LV2_UI_HOST_H

#include "lmmsconfig.h"

#ifdef LMMS_HAVE_LV2_UI

#include <functional>
#include <memory>
#include <lilv/lilv.h>

class QWidget;

namespace lmms
{

class Lv2Proc;

namespace gui
{

class Lv2UiHost
{
public:
	static bool isAvailable(const LilvPlugin* plugin);

	Lv2UiHost(QWidget* parent, const LilvPlugin* plugin, Lv2Proc* proc,
		std::function<void()> closed);
	~Lv2UiHost();

	Lv2UiHost(const Lv2UiHost&) = delete;
	Lv2UiHost& operator=(const Lv2UiHost&) = delete;

	bool isValid() const;
	void show();

private:
	class Impl;
	std::unique_ptr<Impl> m_impl;
};

} // namespace gui
} // namespace lmms

#endif // LMMS_HAVE_LV2_UI

#endif // LMMS_GUI_LV2_UI_HOST_H
