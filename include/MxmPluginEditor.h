/*
 * MxmPluginEditor.h - native/floating editor window for bridge plugins
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

#ifndef MXM_GUI_MXM_PLUGIN_EDITOR_H
#define MXM_GUI_MXM_PLUGIN_EDITOR_H

#include <QWidget>

#include "MxmBridge.h"
#include "mxm_export.h"

namespace mxm
{

namespace bridge
{
class IPlugin;
}

namespace gui
{

/**
	Floating native editor window for a `bridge::IPlugin`.

	Creates a top-level native window and asks the plugin to attach its own
	editor view into it (HWND on Windows, XEmbed window on Linux). This is the
	"native/floating plugin editor window" target: the manufacturer's UI is
	hosted as-is rather than being reparented into MXM's Qt subwindow layout.
*/
class MXM_EXPORT MxmPluginEditor : public QWidget
{
	Q_OBJECT
public:
	MxmPluginEditor(bridge::IPlugin* plugin, QWidget* parent = nullptr);
	~MxmPluginEditor() override;

	MxmPluginEditor(const MxmPluginEditor&) = delete;
	MxmPluginEditor& operator=(const MxmPluginEditor&) = delete;

	//! Show the window and attach the plugin editor.
	void open();
	bool isAttached() const { return m_attached; }

protected:
	void showEvent(QShowEvent* event) override;
	void closeEvent(QCloseEvent* event) override;

private:
	void attach();
	void detach();

	bridge::IPlugin* m_plugin;
	bool m_attached = false;
	bool m_resizingFromPlugin = false;
};

} // namespace gui
} // namespace mxm

#endif // MXM_GUI_MXM_PLUGIN_EDITOR_H
