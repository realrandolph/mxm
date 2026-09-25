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

#include <QPushButton>
#include <QVBoxLayout>

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

	layout->addStretch();
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
