/*
 * Vst3InstrumentView.cpp - view for VST3 instrument plugins
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

#include "Vst3InstrumentView.h"

#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "AutomatableModel.h"
#include "Controls.h"
#include "MxmPluginBridge.h"
#include "MxmPluginEditor.h"
#include "Vst3Instrument.h"

namespace mxm
{
namespace gui
{

Vst3InstrumentView::Vst3InstrumentView(Vst3Instrument* instrument, QWidget* parent)
	: InstrumentView(instrument, parent)
	, m_instrument(instrument)
{
	setAutoFillBackground(true);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(8, 8, 8, 8);

	MxmPluginBridge* bridge = m_instrument->bridge().get();
	if (!bridge)
	{
		return;
	}

	if (bridge->hasGui())
	{
		m_toggleGuiButton = new QPushButton(tr("Show GUI"), this);
		m_toggleGuiButton->setCheckable(true);
		connect(m_toggleGuiButton, &QPushButton::clicked, this, &Vst3InstrumentView::togglePluginUI);
		layout->addWidget(m_toggleGuiButton);
	}

	if (bridge->parameterModelCount() > 0)
	{
		// Keep the control count bounded: some plugins expose thousands of parameters.
		auto* selectorLabel = new QLabel(tr("Parameter"), this);
		auto* selector = new QComboBox(this);
		selector->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
		selector->setMinimumContentsLength(24);
		for (int i = 0; i < bridge->parameterModelCount(); ++i)
		{
			selector->addItem(bridge->parameterName(i));
		}

		auto* controls = new QStackedWidget(this);
		auto knob = std::make_unique<KnobControl>(QString(), controls);
		auto lcd = std::make_unique<LcdControl>(4, controls);
		auto check = std::make_unique<CheckControl>(controls);
		auto* knobControl = knob.get();
		auto* lcdControl = lcd.get();
		auto* checkControl = check.get();
		controls->addWidget(knobControl->topWidget());
		controls->addWidget(lcdControl->topWidget());
		controls->addWidget(checkControl->topWidget());
		m_parameterControls.push_back(std::move(knob));
		m_parameterControls.push_back(std::move(lcd));
		m_parameterControls.push_back(std::move(check));

		auto selectParameter = [bridge, controls, knobControl, lcdControl, checkControl](int index)
		{
			if (index < 0) { return; }
			auto* model = bridge->parameterModel(index);
			const QString name = bridge->parameterName(index);
			if (dynamic_cast<FloatModel*>(model))
			{
				knobControl->setText(name);
				knobControl->setModel(model);
				controls->setCurrentWidget(knobControl->topWidget());
			}
			else if (dynamic_cast<IntModel*>(model))
			{
				lcdControl->setText(name);
				lcdControl->setModel(model);
				controls->setCurrentWidget(lcdControl->topWidget());
			}
			else if (dynamic_cast<BoolModel*>(model))
			{
				checkControl->setText(name);
				checkControl->setModel(model);
				controls->setCurrentWidget(checkControl->topWidget());
			}
		};
		connect(selector, qOverload<int>(&QComboBox::currentIndexChanged), this, selectParameter);
		selectParameter(0);

		layout->addWidget(selectorLabel);
		layout->addWidget(selector);
		layout->addWidget(controls, 1);
	}
}

Vst3InstrumentView::~Vst3InstrumentView()
{
	if (m_editor)
	{
		m_editor->close();
		m_editor->deleteLater();
	}
}

void Vst3InstrumentView::togglePluginUI()
{
	MxmPluginBridge* bridge = m_instrument->bridge().get();
	if (!bridge)
	{
		return;
	}

	if (!m_editor)
	{
		m_editor = new MxmPluginEditor(bridge->plugin(), this);
		connect(m_editor, &QWidget::destroyed, this, [this]()
		{
			m_editor = nullptr;
			if (m_toggleGuiButton) { m_toggleGuiButton->setChecked(false); }
		});
		m_editor->open();
	}
	else if (m_editor->isVisible())
	{
		m_editor->close();
	}
	else
	{
		m_editor->open();
	}

	if (m_toggleGuiButton)
	{
		m_toggleGuiButton->setChecked(m_editor && m_editor->isVisible());
	}
}

} // namespace gui
} // namespace mxm
