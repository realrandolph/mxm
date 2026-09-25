/*
 * MxmPluginEditor.cpp - native/floating editor window for bridge plugins
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

#include "MxmPluginEditor.h"

#include <cstdint>

#include <QCloseEvent>
#include <QShowEvent>

namespace mxm
{
namespace gui
{

MxmPluginEditor::MxmPluginEditor(bridge::IPlugin* plugin, QWidget* parent)
	: QWidget(parent, Qt::Window)
	, m_plugin(plugin)
{
	setAttribute(Qt::WA_NativeWindow, true);
	setAttribute(Qt::WA_DontCreateNativeAncestors, true);
	setAttribute(Qt::WA_DeleteOnClose, false);

	if (m_plugin)
	{
		setWindowTitle(m_plugin->name());
		m_plugin->setEditorResizeCallback([this](int32_t width, int32_t height)
		{
			if (m_resizingFromPlugin || width <= 0 || height <= 0) { return; }
			m_resizingFromPlugin = true;
			resize(width, height);
			m_resizingFromPlugin = false;
		});
	}
}

MxmPluginEditor::~MxmPluginEditor()
{
	detach();
}

void MxmPluginEditor::open()
{
	show();
	raise();
	createWinId();
	attach();
}

void MxmPluginEditor::attach()
{
	if (m_attached || !m_plugin) { return; }

	WId windowId = winId();
	if (!windowId)
	{
		createWinId();
		windowId = winId();
	}

	if (!m_plugin->openEditor(reinterpret_cast<void*>(static_cast<uintptr_t>(windowId))))
	{
		return;
	}
	m_attached = true;

	const QSize size = m_plugin->editorSize();
	if (size.width() > 0 && size.height() > 0)
	{
		resize(size);
	}
}

void MxmPluginEditor::detach()
{
	if (m_attached && m_plugin)
	{
		m_plugin->closeEditor();
	}
	m_attached = false;
}

void MxmPluginEditor::showEvent(QShowEvent* event)
{
	QWidget::showEvent(event);
	createWinId();
}

void MxmPluginEditor::closeEvent(QCloseEvent* event)
{
	detach();
	event->accept();
	hide();
}

} // namespace gui
} // namespace mxm
