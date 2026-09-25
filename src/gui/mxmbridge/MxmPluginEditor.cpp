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

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <QCloseEvent>
#include <QMetaObject>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>

#ifdef MXM_HAVE_X11_EMBED_CONTAINER
#include "X11EmbedContainer.h"

#include <QtX11Extras/QX11Info>
#include <X11/Xlib.h>
#endif

namespace mxm
{
namespace gui
{

#ifdef MXM_HAVE_X11_EMBED_CONTAINER
namespace
{
// Map the embedded client window so it actually renders. Some plugins (notably
// u-he) create their X11 window as an XEmbed child but never map it or set the
// _XEMBED_INFO mapped flag themselves; JUCE-based plugins map themselves, so
// this is a no-op for them.
void mapEmbeddedClient(QWidget* host)
{
	if (auto* container = qobject_cast<QX11EmbedContainer*>(host))
	{
		const WId client = container->clientWinId();
		if (client)
		{
			XMapWindow(QX11Info::display(), client);
			XRaiseWindow(QX11Info::display(), client);
		}
	}
}

// Enumerate the direct children of the root window (i.e. top-level windows).
std::vector<WId> topLevelWindows()
{
	std::vector<WId> result;
	Display* display = QX11Info::display();
	Window root = QX11Info::appRootWindow(QX11Info::appScreen());
	Window rootRet, parentRet;
	Window* children = nullptr;
	unsigned int count = 0;
	if (XQueryTree(display, root, &rootRet, &parentRet, &children, &count))
	{
		for (unsigned int i = 0; i < count; ++i)
		{
			result.push_back(children[i]);
		}
		if (children) { XFree(children); }
	}
	return result;
}

// True if @p window is a window-manager frame that has reparented one of the
// windows we already know about (e.g. the editor window). We must not embed
// such a frame.
bool isAncestorFrame(WId window, const std::vector<WId>& known)
{
	Display* display = QX11Info::display();
	Window rootRet, parentRet;
	Window* children = nullptr;
	unsigned int count = 0;
	if (!XQueryTree(display, window, &rootRet, &parentRet, &children, &count))
	{
		return false;
	}
	bool isFrame = false;
	for (unsigned int i = 0; i < count; ++i)
	{
		if (std::find(known.begin(), known.end(), children[i]) != known.end())
		{
			isFrame = true;
			break;
		}
	}
	if (children) { XFree(children); }
	return isFrame;
}
} // namespace
#endif

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
	connect(qobject_cast<QX11EmbedContainer*>(m_editorHost),
		&QX11EmbedContainer::clientIsEmbedded, this, [this]() { mapEmbeddedClient(m_editorHost); });
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

#ifdef MXM_HAVE_X11_EMBED_CONTAINER
	const std::vector<WId> before = topLevelWindows();
#endif

	attach();
	if (m_attached)
	{
		show();
		raise();
		activateWindow();
	}

#ifdef MXM_HAVE_X11_EMBED_CONTAINER
	// Some plugins (notably u-he) create their editor as a top-level X window
	// rather than as an XEmbed child of the container. Detect any such window
	// and reparent it into the container via embedClient(). Retry briefly for
	// plugins that create their window asynchronously.
	auto embedAttempt = std::make_shared<std::function<void(int)>>();
	*embedAttempt = [this, before, embedAttempt](int attempt)
	{
		auto* container = static_cast<QX11EmbedContainer*>(m_editorHost);
		if (!m_attached || container->clientWinId() != 0)
		{
			return;
		}
		for (WId w : topLevelWindows())
		{
			if (std::find(before.begin(), before.end(), w) != before.end())
			{
				continue;
			}
			if (isAncestorFrame(w, before))
			{
				continue;
			}

			// Capture the plugin's intended size before reparenting resizes it.
			Window rootRet;
			int x = 0, y = 0;
			unsigned int cw = 0, ch = 0, border = 0, depth = 0;
			XGetGeometry(QX11Info::display(), w, &rootRet, &x, &y, &cw, &ch, &border, &depth);

			container->embedClient(w);

			if (cw > 0 && ch > 0)
			{
				const QSize clientSize(static_cast<int>(cw), static_cast<int>(ch));
				if (clientSize != size())
				{
					if (m_plugin->editorIsResizable())
					{
						resize(clientSize);
					}
					else
					{
						setFixedSize(clientSize);
					}
				}
			}
			return;
		}
		if (attempt < 60)
		{
			QTimer::singleShot(50, this, [embedAttempt, attempt]()
			{
				(*embedAttempt)(attempt + 1);
			});
		}
	};
	(*embedAttempt)(0);
#endif
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
		if (m_plugin->editorIsResizable())
		{
			resize(size);
		}
		else
		{
			// Fixed-size UI: prevent the window (and thus the embedded client)
			// from being resized out from under the plugin.
			setFixedSize(size);
		}
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
#ifdef MXM_HAVE_X11_EMBED_CONTAINER
	mapEmbeddedClient(m_editorHost);
#endif
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

	if (!m_plugin->editorIsResizable())
	{
		return;
	}

	// Ask the plugin to constrain the requested size and notify it of the final
	// size via onSize(). The QX11EmbedContainer also resizes the client X window,
	// but onSize() keeps the plugin's internal view geometry in sync so that
	// resizable UIs (e.g. Surge) reflow correctly.
	const QSize accepted = m_plugin->resizeEditor(m_editorHost->size());
	if (accepted.isValid() && accepted != m_editorHost->size())
	{
		m_resizingFromPlugin = true;
		resize(accepted);
		m_resizingFromPlugin = false;
	}
}

} // namespace gui
} // namespace mxm
