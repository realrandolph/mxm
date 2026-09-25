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

#ifdef MXM_HAVE_PLUGIN_EDITOR_X11
#include <QGuiApplication>
#include <X11/Xlib.h>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QNativeInterface>
#else
#include <QtX11Extras/QX11Info>
#endif
#endif

namespace mxm
{
namespace gui
{

#ifdef MXM_HAVE_PLUGIN_EDITOR_X11
namespace
{
// Qt5 exposes the X display through Qt5X11Extras, Qt6 through the X11 native
// interface. This keeps the editor host working on both.
Display* x11Display()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	auto* x11 = qGuiApp ? qGuiApp->nativeInterface<QNativeInterface::QX11Application>() : nullptr;
	return x11 ? x11->display() : nullptr;
#else
	return QX11Info::display();
#endif
}

// The plugin creates its editor window as a child of the mapped native host
// window (kPlatformTypeX11EmbedWindowID), exactly like the Suil/LV2 host. We
// find it as the largest direct child of the host rather than reparenting it
// into a QX11EmbedContainer, which corrupts the plugin's window lifecycle.
WId editorChildWindow(QWidget* host)
{
	if (!host) { return 0; }
	Display* display = x11Display();
	Window rootRet, parentRet;
	Window* children = nullptr;
	unsigned int count = 0;
	WId best = 0;
	qint64 bestArea = 0;
	if (XQueryTree(display, host->winId(), &rootRet, &parentRet, &children, &count))
	{
		for (unsigned int i = 0; i < count; ++i)
		{
			Window r2;
			int x = 0, y = 0;
			unsigned int cw = 0, ch = 0, b = 0, d = 0;
			if (XGetGeometry(display, children[i], &r2, &x, &y, &cw, &ch, &b, &d))
			{
				const qint64 area = static_cast<qint64>(cw) * ch;
				if (area > bestArea)
				{
					bestArea = area;
					best = children[i];
				}
			}
		}
		if (children) { XFree(children); }
	}
	return best;
}

// A plugin-managed child window is not necessarily mapped by the plugin; map it
// explicitly, as the LV2 host does with the widget Suil returns.
void mapEditorWindow(QWidget* host)
{
	const WId child = editorChildWindow(host);
	if (child)
	{
		XWindowAttributes attributes{};
		if (XGetWindowAttributes(x11Display(), child, &attributes)
			&& attributes.map_state == IsUnmapped)
		{
			XMapWindow(x11Display(), child);
			XFlush(x11Display());
		}
	}
}

// Keep the plugin's child window matched to the host window size.
void resizeEditorWindow(QWidget* host)
{
	const WId child = editorChildWindow(host);
	if (child)
	{
		XResizeWindow(x11Display(), child, host->width(), host->height());
	}
}

// Resize the editor window to the plugin's actual window geometry. Some plugins
// report a stale/default size via IPlugView::getSize() (u-he reports 1200x600
// while its real UI is 1550x930), so the plugin window is authoritative.
void synchronizeEditorSize(QWidget* editor, QWidget* host, bridge::IPlugin* plugin)
{
	const WId child = editorChildWindow(host);
	if (!child) { return; }

	Window rootRet;
	int x = 0, y = 0;
	unsigned int cw = 0, ch = 0, border = 0, depth = 0;
	if (!XGetGeometry(x11Display(), child, &rootRet, &x, &y, &cw, &ch, &border, &depth))
	{
		return;
	}
	if (cw == 0 || ch == 0) { return; }

	const QSize childSize(static_cast<int>(cw), static_cast<int>(ch));
	if (childSize == editor->size()) { return; }

	if (plugin->editorIsResizable())
	{
		editor->resize(childSize);
	}
	else
	{
		editor->setFixedSize(childSize);
	}
}

// Enumerate the direct children of the root window (i.e. top-level windows).
std::vector<WId> topLevelWindows()
{
	std::vector<WId> result;
	Display* display = x11Display();
	Window root = DefaultRootWindow(x11Display());
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
	Display* display = x11Display();
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

	// Plain native child window that we give to the plugin as its parent. This
	// mirrors the LV2 native-UI host (Lv2UiHost): QX11EmbedContainer must not be
	// used because it interferes with the plugin's window lifecycle.
	m_editorHost = new QWidget(this);
	m_editorHost->setAttribute(Qt::WA_NativeWindow, true);
	m_editorHost->setAttribute(Qt::WA_DontCreateNativeAncestors, true);
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
	m_editorHost->createWinId();

	// Show (and therefore map) the window and its native host before attaching
	// the plugin, then map the host and flush, exactly like the LV2 UI host.
	show();
	raise();

#ifdef MXM_HAVE_PLUGIN_EDITOR_X11
	const std::vector<WId> before = topLevelWindows();
	const WId parent = m_editorHost->winId();
	if (parent)
	{
		XMapRaised(x11Display(), parent);
		XSync(x11Display(), False);
	}
#endif

	attach();
	if (m_attached)
	{
		raise();
		activateWindow();
	}

#ifdef MXM_HAVE_PLUGIN_EDITOR_X11
	// Map the plugin's child window and match the editor to its real size.
	mapEditorWindow(m_editorHost);
	synchronizeEditorSize(this, m_editorHost, m_plugin);

	// Some plugins create their editor as a top-level window instead of a child
	// of the host. Reparent any such window into the host. Retry briefly for
	// plugins that create their window asynchronously.
	auto embedded = std::make_shared<bool>(false);
	auto embedAttempt = std::make_shared<std::function<void(int)>>();
	*embedAttempt = [this, before, embedded, embedAttempt](int attempt)
	{
		if (!m_attached) { return; }

		if (!*embedded)
		{
			// A conforming plugin has already embedded its editor directly.
			// Stop the top-level fallback before a plugin-owned popup appears and
			// is mistaken for an editor that needs reparenting.
			if (editorChildWindow(m_editorHost))
			{
				*embedded = true;
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
				XReparentWindow(x11Display(), w, m_editorHost->winId(), 0, 0);
				XMapRaised(x11Display(), w);
				XFlush(x11Display());
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

	// Some plugins resize their window asynchronously after embedding. Poll the
	// child's geometry briefly and keep the editor window matched to it.
	auto pollCount = std::make_shared<int>(0);
	auto poll = std::make_shared<std::function<void()>>();
	*poll = [this, pollCount, poll]()
	{
		if (!m_attached) { return; }
		mapEditorWindow(m_editorHost);
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
#ifdef MXM_HAVE_PLUGIN_EDITOR_X11
	// Prefer the plugin's actual window geometry when it is larger than the size
	// it reports via IPlugView::getSize().
	const WId child = editorChildWindow(m_editorHost);
	if (child)
	{
		Window rootRet;
		int x = 0, y = 0;
		unsigned int cw = 0, ch = 0, border = 0, depth = 0;
		if (XGetGeometry(x11Display(), child, &rootRet, &x, &y, &cw, &ch, &border, &depth))
		{
			const QSize childSize(static_cast<int>(cw), static_cast<int>(ch));
			if (static_cast<qint64>(childSize.width()) * childSize.height()
				> static_cast<qint64>(size.width()) * size.height())
			{
				size = childSize;
			}
		}
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
			// Fixed-size UI: prevent the window (and thus the plugin window)
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
#ifdef MXM_HAVE_PLUGIN_EDITOR_X11
	mapEditorWindow(m_editorHost);
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

#ifdef MXM_HAVE_PLUGIN_EDITOR_X11
	// Keep the plugin's child window matched to the host.
	resizeEditorWindow(m_editorHost);
#endif

	if (!m_plugin->editorIsResizable())
	{
		return;
	}

	// Ask the plugin to constrain the requested size and notify it of the final
	// size via onSize() so resizable UIs (e.g. Surge) reflow correctly.
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
