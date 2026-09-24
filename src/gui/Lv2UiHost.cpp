/*
 * Lv2UiHost.cpp - Suil based native LV2 UI host
 *
 * Copyright (c) 2026 dolf <dolfnimmer@proton.me>
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

#include "Lv2UiHost.h"

#ifdef MXM_HAVE_LV2_UI

#include <algorithm>
#include <array>
#include <cstring>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include <QCloseEvent>
#include <QDebug>
#include <QGuiApplication>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <QWidget>
#include <QX11Info>

#include <X11/Xlib.h>

#include <lilv/lilv.h>
#include <lv2/atom/atom.h>
#include <lv2/instance-access/instance-access.h>
#include <lv2/options/options.h>
#include <lv2/parameters/parameters.h>
#include <lv2/ui/ui.h>
#include <lv2/urid/urid.h>
#include <suil/suil.h>

#include "AudioEngine.h"
#include "Engine.h"
#include "Lv2Basics.h"
#include "Lv2Manager.h"
#include "Lv2Proc.h"

namespace mxm::gui
{

namespace
{

void warnUi(const char* message)
{
	qWarning() << "LV2 UI:" << message;
}

void warnUi(const char* message, const char* detail)
{
	qWarning() << "LV2 UI:" << message << detail;
}


class Lv2UiWindow : public QWidget
{
public:
	Lv2UiWindow(QWidget* parent, std::function<void()> closed) :
		QWidget(parent, Qt::Window),
		m_closed(std::move(closed))
	{
		setAttribute(Qt::WA_DeleteOnClose, false);
		setAttribute(Qt::WA_NativeWindow, true);
		setAttribute(Qt::WA_DontCreateNativeAncestors, true);
	}

	void setResizeCallback(std::function<void(const QSize&)> resized)
	{
		m_resized = std::move(resized);
	}

	void requestClose(const char* reason)
	{
		if (m_closePending) { return; }
		m_closePending = true;
		if (reason) { warnUi(reason); }
		hide();
		const auto closed = m_closed;
		QWidget* context = parentWidget() ? parentWidget() : static_cast<QWidget*>(this);
		QTimer::singleShot(0, context, [closed]() { if (closed) { closed(); } });
	}

protected:
	void closeEvent(QCloseEvent* event) override
	{
		event->ignore();
		requestClose(nullptr);
	}

	void resizeEvent(QResizeEvent* event) override
	{
		QWidget::resizeEvent(event);
		if (m_resized) { m_resized(event->size()); }
	}

	void showEvent(QShowEvent* event) override
	{
		QWidget::showEvent(event);
		createWinId();
	}

private:
	std::function<void()> m_closed;
	std::function<void(const QSize&)> m_resized;
	bool m_closePending = false;
};


bool hasStatement(const LilvNode* subject, const char* predicateUri, const char* objectUri)
{
	auto manager = Engine::getLv2Manager();
	AutoLilvNode predicate = manager->uri(predicateUri);
	AutoLilvNode object = objectUri ? manager->uri(objectUri) : AutoLilvNode{};
	AutoLilvNodes values = manager->findNodes(subject, predicate.get(), object.get());
	return values && lilv_nodes_size(values.get()) != 0;
}


bool requiredFeaturesSupported(const LilvNode* uiUri)
{
	static const std::array<const char*, 12> supportedFeatures = {
		LV2_ATOM__eventTransfer,
		LV2_INSTANCE_ACCESS_URI,
		LV2_OPTIONS__options,
		LV2_UI__floatProtocol,
		LV2_UI__idleInterface,
		LV2_UI__fixedSize,
		LV2_UI__noUserResize,
		LV2_UI__parent,
		LV2_UI__portMap,
		LV2_UI__resize,
		LV2_URID__map,
		LV2_URID__unmap,
	};

	auto manager = Engine::getLv2Manager();
	AutoLilvNode requiredFeature = manager->uri(LV2_CORE__requiredFeature);
	AutoLilvNodes required = manager->findNodes(uiUri, requiredFeature.get(), nullptr);
	LILV_FOREACH(nodes, i, required.get())
	{
		const char* uri = lilv_node_as_uri(lilv_nodes_get(required.get(), i));
		if (!uri || std::find_if(supportedFeatures.begin(), supportedFeatures.end(),
			[uri](const char* supported) { return std::strcmp(uri, supported) == 0; }) == supportedFeatures.end())
		{
			return false;
		}
	}

	AutoLilvNode requiredOption = manager->uri(LV2_OPTIONS__requiredOption);
	AutoLilvNodes options = manager->findNodes(uiUri, requiredOption.get(), nullptr);
	LILV_FOREACH(nodes, i, options.get())
	{
		const char* uri = lilv_node_as_uri(lilv_nodes_get(options.get(), i));
		if (!uri || (std::strcmp(uri, LV2_UI__scaleFactor) != 0 &&
			std::strcmp(uri, LV2_PARAMETERS__sampleRate) != 0))
		{
			return false;
		}
	}
	return true;
}


template<typename Function>
bool withBestUi(const LilvPlugin* plugin, const Function& function, bool diagnose)
{
	if (!plugin)
	{
		if (diagnose) { warnUi("plugin descriptor is null"); }
		return false;
	}
	if (QGuiApplication::platformName() != QStringLiteral("xcb"))
	{
		if (diagnose)
		{
			qWarning() << "LV2 UI: native UIs require the xcb Qt platform, got"
				<< QGuiApplication::platformName();
		}
		return false;
	}

	AutoLilvNode x11Ui = Engine::getLv2Manager()->uri(LV2_UI__X11UI);
	LilvUIs* uis = lilv_plugin_get_uis(plugin);
	if (!uis)
	{
		if (diagnose) { warnUi("plugin declares no UIs"); }
		return false;
	}

	const LilvUI* bestUi = nullptr;
	const LilvNode* bestType = nullptr;
	unsigned bestQuality = std::numeric_limits<unsigned>::max();
	LILV_FOREACH(uis, i, uis)
	{
		const LilvUI* ui = lilv_uis_get(uis, i);
		const LilvNode* uiType = nullptr;
		const unsigned quality = lilv_ui_is_supported(ui, suil_ui_supported, x11Ui.get(), &uiType);
		if (quality && quality < bestQuality && requiredFeaturesSupported(lilv_ui_get_uri(ui)))
		{
			bestUi = ui;
			bestType = uiType;
			bestQuality = quality;
		}
	}

	const bool result = bestUi && function(bestUi, bestType);
	if (!bestUi && diagnose) { warnUi("no compatible X11 UI was found"); }
	lilv_uis_free(uis);
	return result;
}

} // namespace


class Lv2UiHost::Impl
{
public:
	Impl(QWidget* parent, const LilvPlugin* plugin, Lv2Proc* proc,
		std::function<void()> closed) :
		m_plugin(plugin),
		m_proc(proc),
		m_closed(std::move(closed))
	{
		if (!m_proc || !m_proc->instanceHandle())
		{
			warnUi("DSP instance is not available");
			return;
		}

		AutoLilvNode pluginName(lilv_plugin_get_name(plugin));
		m_window = new Lv2UiWindow(parent, m_closed);
		m_window->setWindowTitle(QString::fromUtf8(
			pluginName ? lilv_node_as_string(pluginName.get()) : "LV2 UI"));
		m_window->setResizeCallback([this](const QSize& size)
		{
			if (m_resizingFromUi || m_noUserResize || !m_childWindow) { return; }
			if (m_uiResize && m_uiResize->ui_resize)
			{
				m_uiResize->ui_resize(suil_instance_get_handle(m_instance),
					size.width(), size.height());
			}
			else
			{
				XResizeWindow(QX11Info::display(), m_childWindow, size.width(), size.height());
			}
		});
		m_window->show();
		m_window->raise();
		m_window->createWinId();
		m_parentWindow = static_cast<Window>(m_window->winId());
		if (!m_parentWindow)
		{
			warnUi("Qt parent window has a zero X11 id");
			return;
		}
		XMapRaised(QX11Info::display(), m_parentWindow);
		XSync(QX11Info::display(), False);

		m_resize.handle = this;
		m_resize.ui_resize = resize;
		m_sampleRate = Engine::audioEngine()->outputSampleRate();
		m_scaleFactor = static_cast<float>(m_window->devicePixelRatioF());
		m_eventTransfer = Engine::getLv2Manager()->uridMap().map(LV2_ATOM__eventTransfer);

		m_host = suil_host_new(writePort, portIndex, nullptr, nullptr);
		if (!m_host)
		{
			warnUi("suil_host_new failed");
			return;
		}
		if (!withBestUi(plugin, [this](const LilvUI* ui, const LilvNode* type)
			{ return instantiate(ui, type); }, true))
		{
			return;
		}

		m_idle = static_cast<const LV2UI_Idle_Interface*>(
			suil_instance_extension_data(m_instance, LV2_UI__idleInterface));
		m_uiResize = static_cast<const LV2UI_Resize*>(
			suil_instance_extension_data(m_instance, LV2_UI__resize));

		m_lastControlValues.assign(m_proc->portCount(), std::numeric_limits<float>::quiet_NaN());
		synchronize();
		m_timer = new QTimer(m_window);
		m_timer->setInterval(30);
		QObject::connect(m_timer, &QTimer::timeout, m_window, [this]() { idle(); });
		m_timer->start();
	}

	~Impl()
	{
		if (m_timer) { m_timer->stop(); }
		if (m_eventsActive) { m_proc->endUiEvents(); }
		if (m_instance) { suil_instance_free(m_instance); }
		if (m_host) { suil_host_free(m_host); }
		delete m_window;
	}

	bool isValid() const { return m_instance != nullptr && m_window != nullptr; }

	void show()
	{
		if (!m_window) { return; }
		m_window->show();
		m_window->raise();
		m_window->activateWindow();
		if (m_parentWindow)
		{
			XMapRaised(QX11Info::display(), m_parentWindow);
			if (m_childWindow) { XMapRaised(QX11Info::display(), m_childWindow); }
			XFlush(QX11Info::display());
		}
	}

private:
	bool instantiate(const LilvUI* ui, const LilvNode* uiType)
	{
		auto manager = Engine::getLv2Manager();
		const LilvNode* uiUri = lilv_ui_get_uri(ui);
		m_noUserResize = hasStatement(uiUri, LV2_CORE__extensionData, LV2_UI__noUserResize) ||
			hasStatement(uiUri, LV2_CORE__extensionData, LV2_UI__fixedSize) ||
			hasStatement(uiUri, LV2_CORE__requiredFeature, LV2_UI__noUserResize) ||
			hasStatement(uiUri, LV2_CORE__requiredFeature, LV2_UI__fixedSize) ||
			hasStatement(uiUri, LV2_CORE__optionalFeature, LV2_UI__noUserResize) ||
			hasStatement(uiUri, LV2_CORE__optionalFeature, LV2_UI__fixedSize);

		if (hasStatement(uiUri, LV2_OPTIONS__supportedOption, LV2_UI__scaleFactor) ||
			hasStatement(uiUri, LV2_OPTIONS__requiredOption, LV2_UI__scaleFactor))
		{
			m_options.push_back({LV2_OPTIONS_INSTANCE, 0,
				manager->uridMap().map(LV2_UI__scaleFactor), sizeof(float),
				manager->uridMap().map(LV2_ATOM__Float), &m_scaleFactor});
		}
		if (hasStatement(uiUri, LV2_OPTIONS__supportedOption, LV2_PARAMETERS__sampleRate) ||
			hasStatement(uiUri, LV2_OPTIONS__requiredOption, LV2_PARAMETERS__sampleRate))
		{
			m_options.push_back({LV2_OPTIONS_INSTANCE, 0,
				manager->uridMap().map(LV2_PARAMETERS__sampleRate), sizeof(float),
				manager->uridMap().map(LV2_ATOM__Float), &m_sampleRate});
		}
		m_options.push_back({});

		m_features.push_back({LV2_UI__parent, reinterpret_cast<void*>(m_parentWindow)});
		m_features.push_back({LV2_INSTANCE_ACCESS_URI, m_proc->instanceHandle()});
		m_features.push_back({LV2_URID__map, manager->uridMap().mapFeature()});
		m_features.push_back({LV2_URID__unmap, manager->uridMap().unmapFeature()});
		m_features.push_back({LV2_UI__resize, &m_resize});
		m_features.push_back({LV2_UI__idleInterface, nullptr});
		m_features.push_back({LV2_OPTIONS__options, m_options.data()});
		m_featurePointers.reserve(m_features.size() + 1);
		for (auto& feature : m_features) { m_featurePointers.push_back(&feature); }
		m_featurePointers.push_back(nullptr);

		AutoLilvPtr<char> bundlePath(lilv_file_uri_parse(
			lilv_node_as_uri(lilv_ui_get_bundle_uri(ui)), nullptr));
		AutoLilvPtr<char> binaryPath(lilv_file_uri_parse(
			lilv_node_as_uri(lilv_ui_get_binary_uri(ui)), nullptr));
		if (!bundlePath || !binaryPath)
		{
			warnUi("failed to parse UI bundle or binary path");
			return false;
		}

		m_instance = suil_instance_new(m_host, this, LV2_UI__X11UI,
			lilv_node_as_uri(lilv_plugin_get_uri(m_plugin)), lilv_node_as_uri(uiUri),
			lilv_node_as_uri(uiType), bundlePath.get(), binaryPath.get(), m_featurePointers.data());
		if (!m_instance)
		{
			warnUi("suil_instance_new failed", lilv_node_as_uri(uiUri));
			return false;
		}
		m_proc->beginUiEvents();
		m_eventsActive = true;

		m_childWindow = static_cast<Window>(reinterpret_cast<uintptr_t>(
			suil_instance_get_widget(m_instance)));
		if (!m_childWindow)
		{
			warnUi("Suil returned a null X11 widget");
			return false;
		}
		if (m_childWindow != m_parentWindow)
		{
			XMapRaised(QX11Info::display(), m_childWindow);
			Window root = 0;
			int x = 0;
			int y = 0;
			unsigned width = 0;
			unsigned height = 0;
			unsigned border = 0;
			unsigned depth = 0;
			if (XGetGeometry(QX11Info::display(), m_childWindow, &root, &x, &y,
				&width, &height, &border, &depth) && width > 1 && height > 1)
			{
				m_resizingFromUi = true;
				m_window->resize(static_cast<int>(width), static_cast<int>(height));
				m_resizingFromUi = false;
			}
		}
		XFlush(QX11Info::display());
		return true;
	}

	void idle()
	{
		if (m_idle && m_idle->idle(suil_instance_get_handle(m_instance)) != 0)
		{
			m_timer->stop();
			m_window->requestClose(nullptr);
			return;
		}

		synchronize();
		m_proc->drainUiEvents([this](uint32_t port, uint32_t size, const void* data)
		{
			suil_instance_port_event(m_instance, port, size, m_eventTransfer, data);
		});
	}

	void synchronize()
	{
		for (uint32_t i = 0; i < m_proc->portCount(); ++i)
		{
			float value = 0.0f;
			if (m_proc->uiControlValue(i, value) && value != m_lastControlValues[i])
			{
				suil_instance_port_event(m_instance, i, sizeof(value), 0, &value);
				m_lastControlValues[i] = value;
			}
		}
	}

	static void writePort(SuilController controller, uint32_t port, uint32_t size,
		uint32_t protocol, const void* buffer)
	{
		auto self = static_cast<Impl*>(controller);
		if (protocol == 0 && size == sizeof(float) && buffer)
		{
			const float value = *static_cast<const float*>(buffer);
			if (self->m_proc->setUiControlValue(port, value) && port < self->m_lastControlValues.size())
			{
				self->m_lastControlValues[port] = value;
			}
		}
		else if (!self->m_proc->enqueueUiEvent(port, size, protocol, buffer))
		{
			qWarning() << "LV2 UI: dropped unsupported or oversized port event" << port
				<< "protocol" << protocol << "size" << size;
		}
	}

	static uint32_t portIndex(SuilController controller, const char* symbol)
	{
		return static_cast<Impl*>(controller)->m_proc->portIndex(symbol);
	}

	static int resize(LV2UI_Feature_Handle handle, int width, int height)
	{
		auto self = static_cast<Impl*>(handle);
		if (width <= 0 || height <= 0) { return 1; }
		self->m_resizingFromUi = true;
		self->m_window->resize(width, height);
		if (self->m_noUserResize) { self->m_window->setFixedSize(width, height); }
		if (self->m_childWindow && self->m_childWindow != self->m_parentWindow)
		{
			XResizeWindow(QX11Info::display(), self->m_childWindow, width, height);
		}
		self->m_resizingFromUi = false;
		return 0;
	}

	const LilvPlugin* m_plugin = nullptr;
	Lv2Proc* m_proc = nullptr;
	std::function<void()> m_closed;
	Lv2UiWindow* m_window = nullptr;
	QTimer* m_timer = nullptr;
	SuilHost* m_host = nullptr;
	SuilInstance* m_instance = nullptr;
	const LV2UI_Idle_Interface* m_idle = nullptr;
	const LV2UI_Resize* m_uiResize = nullptr;
	LV2UI_Resize m_resize{};
	std::vector<LV2_Options_Option> m_options;
	std::vector<LV2_Feature> m_features;
	std::vector<const LV2_Feature*> m_featurePointers;
	std::vector<float> m_lastControlValues;
	float m_sampleRate = 0.0f;
	float m_scaleFactor = 1.0f;
	LV2_URID m_eventTransfer = 0;
	Window m_parentWindow = 0;
	Window m_childWindow = 0;
	bool m_noUserResize = false;
	bool m_resizingFromUi = false;
	bool m_eventsActive = false;
};


bool Lv2UiHost::isAvailable(const LilvPlugin* plugin)
{
	return withBestUi(plugin, [](const LilvUI*, const LilvNode*) { return true; }, false);
}


Lv2UiHost::Lv2UiHost(QWidget* parent, const LilvPlugin* plugin, Lv2Proc* proc,
	std::function<void()> closed) :
	m_impl(std::make_unique<Impl>(parent, plugin, proc, std::move(closed)))
{
}


Lv2UiHost::~Lv2UiHost() = default;


bool Lv2UiHost::isValid() const
{
	return m_impl->isValid();
}


void Lv2UiHost::show()
{
	m_impl->show();
}

} // namespace mxm::gui

#endif // MXM_HAVE_LV2_UI
