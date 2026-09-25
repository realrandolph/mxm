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

// Resize the editor window to the embedded client's actual X geometry. Some
// plugins report a stale/default size via IPlugView::getSize() (u-he reports
// 1200x600 while its real UI is 1550x930), so the client window is the
// authoritative size source once it has been embedded.
void synchronizeEditorSize(QWidget* editor, QWidget* host, bridge::IPlugin* plugin)
{
	auto* container = qobject_cast<QX11EmbedContainer*>(host);
	if (!container) { return; }

	const WId client = container->clientWinId();
	if (!client) { return; }

	Window rootRet;
	int x = 0, y = 0;
	unsigned int cw = 0, ch = 0, border = 0, depth = 0;
	if (!XGetGeometry(QX11Info::display(), client, &rootRet, &x, &y, &cw, &ch, &border, &depth))
	{
		return;
	}
	if (cw == 0 || ch == 0) { return; }

	const QSize clientSize(static_cast<int>(cw), static_cast<int>(ch));
	if (clientSize == editor->size()) { return; }

	if (plugin->editorIsResizable())
	{
		editor->resize(clientSize);
	}
	else
	{
		editor->setFixedSize(clientSize);
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

// Returns the geometry of the largest direct child of the container. The
// plugin's editor window is much larger than the container's own helper windows
// (the 1x1 focus proxy and the Qt user-time window), so this reliably finds the
// plugin window even before the container's acceptClient resizes it.
QSize largestHostChildSize(QWidget* host)
{
	auto* container = qobject_cast<QX11EmbedContainer*>(host);
	if (!container) { return QSize(); }

	Display* display = QX11Info::display();
	Window rootRet, parentRet;
	Window* children = nullptr;
	unsigned int count = 0;
	QSize best;
	if (XQueryTree(display, container->winId(), &rootRet, &parentRet, &children, &count))
	{
		for (unsigned int i = 0; i < count; ++i)
		{
			Window r2;
			int x = 0, y = 0;
			unsigned int cw = 0, ch = 0, b = 0, d = 0;
			if (XGetGeometry(display, children[i], &r2, &x, &y, &cw, &ch, &b, &d))
			{
				if (static_cast<qint64>(cw) * ch > static_cast<qint64>(best.width()) * best.height())
				{
					best = QSize(static_cast<int>(cw), static_cast<int>(ch));
				}
			}
		}
		if (children) { XFree(children); }
	}
	return best;
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
		&QX11EmbedContainer::clientIsEmbedded, this, [this]()
		{
			mapEmbeddedClient(m_editorHost);
			synchronizeEditorSize(this, m_editorHost, m_plugin);
		});
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

	// Show (and therefore map) the window and its container before attaching
	// the plugin. Some plugins (u-he) only create their editor as an XEmbed
	// child when the parent window is already mapped, and otherwise fall back
	// to a top-level window.
	show();

#ifdef MXM_HAVE_X11_EMBED_CONTAINER
	const std::vector<WId> before = topLevelWindows();
#endif

	attach();
	if (m_attached)
	{
		raise();
		activateWindow();
	}

#ifdef MXM_HAVE_X11_EMBED_CONTAINER
	// Some plugins (notably u-he) create their editor as a top-level X window
	// rather than as an XEmbed child of the container. Detect any such window
	// and reparent it into the container via embedClient(). Retry briefly for
	// plugins that create their window asynchronously. We track completion with
	// our own flag rather than clientWinId(), because the container may have
	// already adopted a Qt-internal window (the NET_WM user-time window) as a
	// spurious client.
	auto embedded = std::make_shared<bool>(false);
	auto embedAttempt = std::make_shared<std::function<void(int)>>();
	*embedAttempt = [this, before, embedded, embedAttempt](int attempt)
	{
		if (!m_attached) { return; }
		auto* container = static_cast<QX11EmbedContainer*>(m_editorHost);

		if (!*embedded)
		{
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

				// Discard any spurious client so the container accepts the
				// plugin's real window instead of rejecting it.
				if (container->clientWinId() != 0)
				{
					container->discardClient();
				}
				container->embedClient(w);
				*embedded = true;
				return;
			}
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

	// Some plugins (u-he) resize their X window asynchronously after embedding
	// (growing from an initial 1200x600 to their real 1550x930 UI). Poll the
	// client's geometry briefly and keep the editor window matched to it.
	auto pollCount = std::make_shared<int>(0);
	auto poll = std::make_shared<std::function<void()>>();
	*poll = [this, pollCount, poll]()
	{
		if (!m_attached) { return; }
		synchronizeEditorSize(this, m_editorHost, m_plugin);
		if (++(*pollCount) < 50)
		{
			QTimer::singleShot(100, this, [poll]() { (*poll)(); });
		}
	};
	QTimer::singleShot(100, this, [poll]() { (*poll)(); });
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

	QSize size = m_plugin->editorSize();
#ifdef MXM_HAVE_X11_EMBED_CONTAINER
	// Some plugins (u-he) report a stale/default size via IPlugView::getSize()
	// but create their editor window at the real UI size. Prefer the plugin
	// window's actual geometry when it is larger, so the UI is not clipped.
	const QSize childSize = largestHostChildSize(m_editorHost);
	if (static_cast<qint64>(childSize.width()) * childSize.height()
		> static_cast<qint64>(size.width()) * size.height())
	{
		size = childSize;
	}
#endif

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
