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

#include <QGridLayout>
#include <QPushButton>
#include <QScrollArea>
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

	// Generic parameter controls so every parameter is discoverable and
	// automatable from MXM's own UI.
	if (bridge->parameterModelCount() > 0)
	{
		auto* container = new QWidget(this);
		auto* grid = new QGridLayout(container);
		grid->setContentsMargins(0, 0, 0, 0);
		grid->setSpacing(10);

		for (int i = 0; i < bridge->parameterModelCount(); ++i)
		{
			AutomatableModel* model = bridge->parameterModel(i);
			Control* control = nullptr;
			if (dynamic_cast<FloatModel*>(model))
			{
				control = new KnobControl(bridge->parameterName(i), container);
			}
			else if (dynamic_cast<IntModel*>(model))
			{
				control = new LcdControl(4, container);
				control->setText(bridge->parameterName(i));
			}
			else if (dynamic_cast<BoolModel*>(model))
			{
				control = new CheckControl(container);
				control->setText(bridge->parameterName(i));
			}
			if (!control)
			{
				continue;
			}

			control->setModel(model);
			m_parameterWidgets.push_back(control->topWidget());

			const int row = i / 4;
			const int col = i % 4;
			grid->addWidget(control->topWidget(), row, col, Qt::AlignCenter);
		}
		grid->setRowStretch((bridge->parameterModelCount() + 3) / 4, 1);

		auto* scroll = new QScrollArea(this);
		scroll->setWidget(container);
		scroll->setWidgetResizable(true);
		scroll->setMinimumHeight(64);
		layout->addWidget(scroll, 1);
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
