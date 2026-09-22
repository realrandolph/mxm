/*
 * Lv2ViewBase.cpp - base class for Lv2 plugin views
 *
 * Copyright (c) 2018-2023 Johannes Lorenz <jlsf2013$users.sourceforge.net, $=@>
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

#include "Lv2ViewBase.h"

#ifdef LMMS_HAVE_LV2

#include <QGridLayout>
#include <QPushButton>
#include <QSignalBlocker>
#include <QHBoxLayout>
#include <QLabel>
#include <lilv/lilv.h>
#include <lv2/port-props/port-props.h>

#include "AudioEngine.h"
#include "Controls.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "embed.h"
#include "FontHelper.h"
#include "lmms_math.h"
#include "Lv2ControlBase.h"
#include "Lv2Manager.h"
#include "Lv2Proc.h"
#include "Lv2Ports.h"
#ifdef LMMS_HAVE_LV2_UI
#include "Lv2UiHost.h"
#endif
#include "MainWindow.h"
#include "SubWindow.h"


namespace lmms::gui
{


Lv2ViewProc::Lv2ViewProc(QWidget* parent, Lv2Proc* proc, int colNum) :
	LinkedModelGroupView (parent, proc, colNum)
{
	class SetupTheWidget : public Lv2Ports::ConstVisitor
	{
	public:
		QWidget* m_parent; // input
		const LilvNode* m_commentUri; // input
		Control* m_control = nullptr; // output
		void visit(const Lv2Ports::Control& port) override
		{
			if (port.m_flow == Lv2Ports::Flow::Input)
			{
				using PortVis = Lv2Ports::Vis;

				switch (port.m_vis)
				{
					case PortVis::Generic:
						m_control = new KnobControl(port.name(), m_parent);
						break;
					case PortVis::Integer:
					{
						sample_rate_t sr = Engine::audioEngine()->outputSampleRate();
						auto pMin = port.min(sr);
						auto pMax = port.max(sr);
						int numDigits = std::max(numDigitsAsInt(pMin), numDigitsAsInt(pMax));
						m_control = new LcdControl(numDigits, m_parent);
						break;
					}
					case PortVis::Enumeration:
						m_control = new ComboControl(m_parent);
						break;
					case PortVis::Toggled:
						m_control = new CheckControl(m_parent);
						break;
				}
				m_control->setText(port.name());

				AutoLilvNodes props(lilv_port_get_value(
					port.m_plugin, port.m_port, m_commentUri));
				LILV_FOREACH(nodes, itr, props.get())
				{
					const LilvNode* nod = lilv_nodes_get(props.get(), itr);
					m_control->topWidget()->setToolTip(lilv_node_as_string(nod));
					break;
				}
			}
		}
	};

	AutoLilvNode commentUri = uri(LILV_NS_RDFS "comment");
	proc->foreach_port(
		[this, &commentUri](const Lv2Ports::PortBase* port)
		{
			if(!lilv_port_has_property(port->m_plugin, port->m_port,
										uri(LV2_PORT_PROPS__notOnGUI).get()))
			{
				SetupTheWidget setup;
				setup.m_parent = this;
				setup.m_commentUri = commentUri.get();
				port->accept(setup);

				if (setup.m_control)
				{
					addControl(setup.m_control,
						lilv_node_as_string(lilv_port_get_symbol(
							port->m_plugin, port->m_port)),
						port->name().toUtf8().data(),
						false);
				}
			}
		});
}




AutoLilvNode Lv2ViewProc::uri(const char *uriStr)
{
	return Engine::getLv2Manager()->uri(uriStr);
}




Lv2ViewBase::Lv2ViewBase(QWidget* meAsWidget, Lv2ControlBase *ctrlBase) :
	m_pluginWidget(meAsWidget),
	m_ctrlBase(ctrlBase),
	m_helpWindowEventFilter(this)
{
	auto grid = new QGridLayout(meAsWidget);

	auto btnBox = new QHBoxLayout();
	if (/* DISABLES CODE */ (false))
	{
		m_reloadPluginButton = new QPushButton(QObject::tr("Reload Plugin"),
			meAsWidget);
		btnBox->addWidget(m_reloadPluginButton, 0);
	}

#ifdef LMMS_HAVE_LV2_UI
	// A UI using instance-access can only control one DSP instance. Mono LV2s
	// are duplicated for stereo by LMMS, so their generic controls remain the
	// safe, synchronized interface.
	if (ctrlBase->processorCount() == 1 && Lv2UiHost::isAvailable(ctrlBase->getPlugin()))
	{
		m_toggleUIButton = new QPushButton(QObject::tr("Show GUI"),
											meAsWidget);
		m_toggleUIButton->setCheckable(true);
		m_toggleUIButton->setChecked(false);
		m_toggleUIButton->setIcon(embed::getIconPixmap("zoom"));
		m_toggleUIButton->setFont(adjustedToPixelSize(m_toggleUIButton->font(), SMALL_FONT_SIZE));
		btnBox->addWidget(m_toggleUIButton, 0);
	}
#endif
	btnBox->addStretch(1);

	meAsWidget->setAcceptDrops(true);

	// note: the lifetime of C++ objects ends after the top expression in the
	// expression syntax tree, so the AutoLilvNode gets freed after the function
	// has been called
	AutoLilvNodes props(lilv_plugin_get_value(ctrlBase->getPlugin(),
				uri(LILV_NS_RDFS "comment").get()));
	LILV_FOREACH(nodes, itr, props.get())
	{
		const LilvNode* node = lilv_nodes_get(props.get(), itr);
		auto infoLabel = new QLabel(QString(lilv_node_as_string(node)).trimmed() + "\n");
		infoLabel->setWordWrap(true);
		infoLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);

		m_helpButton = new QPushButton(QObject::tr("Help"));
		m_helpButton->setCheckable(true);
		btnBox->addWidget(m_helpButton);

		m_helpWindow = getGUI()->mainWindow()->addWindowedWidget(infoLabel);
		m_helpWindow->setSizePolicy(QSizePolicy::Expanding,
									QSizePolicy::Expanding);
		m_helpWindow->installEventFilter(&m_helpWindowEventFilter);
		m_helpWindow->setAttribute(Qt::WA_DeleteOnClose, false);
		m_helpWindow->hide();

		break;
	}

	if (m_reloadPluginButton || m_toggleUIButton || m_helpButton)
	{
		grid->addLayout(btnBox, Rows::ButtonRow, 0, 1, m_colNum);
	}
	else { delete btnBox; }

	m_procView = new Lv2ViewProc(meAsWidget, ctrlBase->control(0), m_colNum);
	grid->addWidget(m_procView, Rows::ProcRow, 0);

	ctrlBase->setUiCloseCallback([this]() { closeNativeUi(); });
}




Lv2ViewBase::~Lv2ViewBase() {
	if (m_ctrlBase) { m_ctrlBase->setUiCloseCallback({}); }
	closeNativeUi();
	closeHelpWindow();
}




void Lv2ViewBase::toggleUI()
{
#ifdef LMMS_HAVE_LV2_UI
	if (!m_toggleUIButton)
	{
		qWarning() << "LV2 UI: Show GUI was requested without a toggle button";
		return;
	}
	if (!m_toggleUIButton->isChecked())
	{
		m_uiHost.reset();
		return;
	}
	if (!m_ctrlBase || !m_ctrlBase->getPlugin() || !m_ctrlBase->control(0))
	{
		qWarning() << "LV2 UI: Show GUI was requested without a plugin instance";
		QSignalBlocker blocker(m_toggleUIButton);
		m_toggleUIButton->setChecked(false);
		return;
	}

	const char* pluginUri = lilv_node_as_uri(lilv_plugin_get_uri(m_ctrlBase->getPlugin()));
	auto uiHost = std::make_unique<Lv2UiHost>(m_pluginWidget, m_ctrlBase->getPlugin(),
		m_ctrlBase->control(0), [this]()
		{
			QSignalBlocker blocker(m_toggleUIButton);
			if (m_toggleUIButton) { m_toggleUIButton->setChecked(false); }
			m_uiHost.reset();
		});
	if (!uiHost->isValid())
	{
		qWarning() << "LV2 UI: failed to instantiate native UI for" << pluginUri;
		QSignalBlocker blocker(m_toggleUIButton);
		m_toggleUIButton->setChecked(false);
		return;
	}

	m_uiHost = std::move(uiHost);
	m_uiHost->show();
#endif // LMMS_HAVE_LV2_UI
}




void Lv2ViewBase::closeNativeUi()
{
#ifdef LMMS_HAVE_LV2_UI
	m_uiHost.reset();
#endif
	if (m_toggleUIButton)
	{
		QSignalBlocker blocker(m_toggleUIButton);
		m_toggleUIButton->setChecked(false);
	}
}




void Lv2ViewBase::toggleHelp(bool visible)
{
	if (m_helpWindow)
	{
		if (visible) { m_helpWindow->show(); m_helpWindow->raise(); }
		else { m_helpWindow->hide(); }
	}
}




void Lv2ViewBase::closeHelpWindow()
{
	if (m_helpWindow) { m_helpWindow->close(); }
}




void Lv2ViewBase::modelChanged(Lv2ControlBase *ctrlBase)
{
	closeNativeUi();
	if (m_ctrlBase && m_ctrlBase != ctrlBase) { m_ctrlBase->setUiCloseCallback({}); }
	m_ctrlBase = ctrlBase;
	m_ctrlBase->setUiCloseCallback([this]() { closeNativeUi(); });

	LinkedModelGroupsView::modelChanged(ctrlBase);
}




AutoLilvNode Lv2ViewBase::uri(const char *uriStr)
{
	return Engine::getLv2Manager()->uri(uriStr);
}




void Lv2ViewBase::onHelpWindowClosed()
{
	m_helpButton->setChecked(true);
}




HelpWindowEventFilter::HelpWindowEventFilter(Lv2ViewBase* viewBase) :
	m_viewBase(viewBase) {}




bool HelpWindowEventFilter::eventFilter(QObject* , QEvent* event)
{
	if (event->type() == QEvent::Close) {
		m_viewBase->m_helpButton->setChecked(false);
		return true;
	}
	return false;
}


} // namespace lmms::gui

#endif // LMMS_HAVE_LV2
