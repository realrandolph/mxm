/*
 * Vst3EffectControlDialog.cpp - control dialog for VST3 effect plugins
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

#include "Vst3EffectControlDialog.h"

#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "AutomatableModel.h"
#include "Controls.h"
#include "MxmPluginBridge.h"
#include "MxmPluginEditor.h"
#include "Vst3Effect.h"
#include "Vst3EffectControls.h"

namespace mxm
{
namespace gui
{

Vst3EffectControlDialog::Vst3EffectControlDialog(Vst3EffectControls* controls)
	: EffectControlDialog(controls)
	, m_controls(controls)
{
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(8, 8, 8, 8);

	MxmPluginBridge* bridge = m_controls->vst3Effect()->bridge().get();
	if (!bridge)
	{
		return;
	}

	if (bridge->hasGui())
	{
		m_toggleGuiButton = new QPushButton(tr("Show GUI"), this);
		m_toggleGuiButton->setCheckable(true);
		connect(m_toggleGuiButton, &QPushButton::clicked, this, &Vst3EffectControlDialog::togglePluginUI);
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

		auto* parameterControls = new QStackedWidget(this);
		auto knob = std::make_unique<KnobControl>(QString(), parameterControls);
		auto lcd = std::make_unique<LcdControl>(4, parameterControls);
		auto check = std::make_unique<CheckControl>(parameterControls);
		auto* knobControl = knob.get();
		auto* lcdControl = lcd.get();
		auto* checkControl = check.get();
		parameterControls->addWidget(knobControl->topWidget());
		parameterControls->addWidget(lcdControl->topWidget());
		parameterControls->addWidget(checkControl->topWidget());
		m_parameterControls.push_back(std::move(knob));
		m_parameterControls.push_back(std::move(lcd));
		m_parameterControls.push_back(std::move(check));

		auto selectParameter = [bridge, parameterControls, knobControl, lcdControl, checkControl](int index)
		{
			if (index < 0) { return; }
			auto* model = bridge->parameterModel(index);
			const QString name = bridge->parameterName(index);
			if (dynamic_cast<FloatModel*>(model))
			{
				knobControl->setText(name);
				knobControl->setModel(model);
				parameterControls->setCurrentWidget(knobControl->topWidget());
			}
			else if (dynamic_cast<IntModel*>(model))
			{
				lcdControl->setText(name);
				lcdControl->setModel(model);
				parameterControls->setCurrentWidget(lcdControl->topWidget());
			}
			else if (dynamic_cast<BoolModel*>(model))
			{
				checkControl->setText(name);
				checkControl->setModel(model);
				parameterControls->setCurrentWidget(checkControl->topWidget());
			}
		};
		connect(selector, qOverload<int>(&QComboBox::currentIndexChanged), this, selectParameter);
		selectParameter(0);

		layout->addWidget(selectorLabel);
		layout->addWidget(selector);
		layout->addWidget(parameterControls, 1);
	}
}

Vst3EffectControlDialog::~Vst3EffectControlDialog()
{
	if (m_editor)
	{
		m_editor->close();
		m_editor->deleteLater();
	}
}

void Vst3EffectControlDialog::togglePluginUI()
{
	MxmPluginBridge* bridge = m_controls->vst3Effect()->bridge().get();
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
