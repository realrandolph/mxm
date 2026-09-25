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
#include <QMetaObject>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>

#ifdef MXM_HAVE_X11_EMBED_CONTAINER
#include "X11EmbedContainer.h"
#endif

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
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

#ifdef MXM_HAVE_X11_EMBED_CONTAINER
	m_editorHost = new QX11EmbedContainer(this);
#else
	m_editorHost = new QWidget(this);
	m_editorHost->setAttribute(Qt::WA_NativeWindow, true);
	m_editorHost->setAttribute(Qt::WA_DontCreateNativeAncestors, true);
#endif
	layout->addWidget(m_editorHost);

	if (m_plugin)
	{
		setWindowTitle(m_plugin->name());
		m_plugin->setEditorResizeCallback([this](int32_t width, int32_t height)
		{
			if (width <= 0 || height <= 0) { return; }
			// resizeView may be invoked from the plugin's own UI thread; marshal
			// the resize back onto the Qt GUI thread so we never touch a QWidget
			// from a foreign thread.
			QMetaObject::invokeMethod(this, [this, width, height]()
			{
				if (m_resizingFromPlugin) { return; }
				m_resizingFromPlugin = true;
				resize(width, height);
				m_resizingFromPlugin = false;
			}, Qt::QueuedConnection);
		});
	}
}

MxmPluginEditor::~MxmPluginEditor()
{
	// Clear the resize callback first so the plugin can never invoke a
	// callback on a half-destroyed editor.
	if (m_plugin)
	{
		m_plugin->setEditorResizeCallback(nullptr);
	}
	detach();
}

void MxmPluginEditor::open()
{
	createWinId();
	m_editorHost->createWinId();
	attach();
	if (m_attached)
	{
		show();
		raise();
		activateWindow();
	}
}

void MxmPluginEditor::attach()
{
	if (m_attached || !m_plugin) { return; }

	WId windowId = m_editorHost->winId();
	if (!windowId)
	{
		m_editorHost->createWinId();
		windowId = m_editorHost->winId();
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
}

void MxmPluginEditor::closeEvent(QCloseEvent* event)
{
	event->accept();
	hide();

	// Defer the actual detach (which destroys the plugin's editor view) to the
	// next event loop iteration, mirroring the LV2 native-UI host. Tearing the
	// view down synchronously inside the close event can re-enter the plugin
	// while it is still handling the close and cause lifecycle crashes.
	if (m_attached && m_plugin)
	{
		m_attached = false;
		auto* plugin = m_plugin;
		QTimer::singleShot(0, this, [plugin]() { plugin->closeEditor(); });
	}
}

void MxmPluginEditor::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	if (!m_attached || !m_plugin || m_resizingFromPlugin)
	{
		return;
	}

#ifdef MXM_HAVE_X11_EMBED_CONTAINER
	// On X11 the QX11EmbedContainer resizes the client window itself whenever
	// the container is resized. Calling IPlugView::onSize() here as well would
	// fight that and cause resize feedback loops / visual corruption.
#else
	// No native size propagation: tell the plugin about its new size explicitly.
	const QSize accepted = m_plugin->editorIsResizable()
		? m_plugin->resizeEditor(m_editorHost->size())
		: m_plugin->editorSize();
	if (accepted.isValid() && accepted != m_editorHost->size())
	{
		m_resizingFromPlugin = true;
		resize(accepted);
		m_resizingFromPlugin = false;
	}
#endif
}

} // namespace gui
} // namespace mxm
