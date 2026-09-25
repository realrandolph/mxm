/*
 * MxmPluginBridge.cpp - format-neutral LMMS-side mapping of the plugin bridge
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

#include "MxmPluginBridge.h"

#include <cmath>
#include <limits>

#include <QDomDocument>
#include <QDomElement>

#include "AudioEngine.h"
#include "Engine.h"
#include "Midi.h"
#include "MidiEvent.h"
#include "SampleFrame.h"
#include "Song.h"
#include "ValueBuffer.h"

namespace mxm
{

MxmPluginBridge::MxmPluginBridge(Model* meAsModel, std::unique_ptr<bridge::IPlugin> plugin)
	: m_meAsModel(meAsModel)
	, m_plugin(std::move(plugin))
{
	if (m_plugin)
	{
		m_plugin->setHost(this);
	}
}

MxmPluginBridge::~MxmPluginBridge()
{
	if (m_plugin)
	{
		m_plugin->closeEditor();
		m_plugin->terminate();
	}
}

void MxmPluginBridge::init()
{
	if (m_plugin && m_plugin->isValid())
	{
		createParameterModels();
		syncModelsFromPlugin();
	}
}

void MxmPluginBridge::createParameterModels()
{
	const int count = m_plugin->parameterCount();
	m_parameters.reserve(static_cast<std::size_t>(count));
	for (int i = 0; i < count; ++i)
	{
		bridge::ParameterInfo info;
		if (!m_plugin->parameterInfo(i, info))
		{
			continue;
		}
		if (info.flags & (bridge::ParameterInfo::IsHidden | bridge::ParameterInfo::IsReadOnly
			| bridge::ParameterInfo::IsProgramChange))
		{
			continue;
		}

		AutomatableModel* model = nullptr;
		if (info.stepCount == 1)
		{
			model = new BoolModel(info.defaultNormalizedValue >= 0.5f, m_meAsModel, info.name);
		}
		else if (info.stepCount > 1)
		{
			const int def = static_cast<int>(std::lround(info.defaultNormalizedValue * info.stepCount));
			model = new IntModel(def, 0, info.stepCount, m_meAsModel, info.name);
		}
		else
		{
			model = new FloatModel(info.defaultNormalizedValue, 0.0f, 1.0f, 0.001f, m_meAsModel, info.name);
		}

		const int index = static_cast<int>(m_parameters.size());
		m_parameters.push_back({info.id, info.stepCount, model});
		m_pending.emplace_back(false);

		QObject::connect(model, &AutomatableModel::dataChanged,
			[this, index]() { onParameterModelChanged(index); });	}
}

AutomatableModel* MxmPluginBridge::parameterModel(int index) const
{
	return m_parameters[static_cast<std::size_t>(index)].model;
}

QString MxmPluginBridge::parameterName(int index) const
{
	return m_parameters[static_cast<std::size_t>(index)].model->displayName();
}

uint32_t MxmPluginBridge::parameterId(int index) const
{
	return m_parameters[static_cast<std::size_t>(index)].id;
}

float MxmPluginBridge::fromModelValue(int index, float modelValue) const
{
	const Parameter& p = m_parameters[static_cast<std::size_t>(index)];
	if (p.stepCount > 1)
	{
		return modelValue / static_cast<float>(p.stepCount);
	}
	return modelValue;
}

float MxmPluginBridge::normalizedValue(int index) const
{
	const Parameter& p = m_parameters[static_cast<std::size_t>(index)];
	return fromModelValue(index, p.model->value<float>());
}

void MxmPluginBridge::setModelFromNormalized(int index, float normalized, bool markPending)
{
	Parameter& p = m_parameters[static_cast<std::size_t>(index)];
	if (p.stepCount == 1)
	{
		static_cast<BoolModel*>(p.model)->setValue(normalized >= 0.5f);
	}
	else if (p.stepCount > 1)
	{
		static_cast<IntModel*>(p.model)->setValue(
			static_cast<int>(std::lround(normalized * static_cast<float>(p.stepCount))));
	}
	else
	{
		static_cast<FloatModel*>(p.model)->setValue(normalized);
	}
}

void MxmPluginBridge::onParameterModelChanged(int index)
{
	if (m_loading || m_syncingFromPlugin.load(std::memory_order_relaxed))
	{
		return;
	}

	m_pending[static_cast<std::size_t>(index)].store(true, std::memory_order_relaxed);

	// Deliver the change immediately while the transport is stopped so the
	// plugin reflects it without a process() call. Automation only runs while
	// playing or exporting, so this branch is only taken on the UI thread.
	Song* song = Engine::getSong();
	if (!song || song->isStopped())
	{
		m_plugin->setParameterValue(m_parameters[static_cast<std::size_t>(index)].id,
			normalizedValue(index));
	}
}

void MxmPluginBridge::parameterEdited(uint32_t id, float valueNormalized)
{
	for (std::size_t i = 0; i < m_parameters.size(); ++i)
	{
		if (m_parameters[i].id == id)
		{
			m_syncingFromPlugin.store(true, std::memory_order_relaxed);
			setModelFromNormalized(static_cast<int>(i), valueNormalized, false);
			m_syncingFromPlugin.store(false, std::memory_order_relaxed);
			return;
		}
	}
}

void MxmPluginBridge::restartRequested(int32_t /*flags*/)
{
}

void MxmPluginBridge::queueAllPendingParameters()
{
	const int frames = Engine::audioEngine()->framesPerPeriod();
	for (std::size_t i = 0; i < m_parameters.size(); ++i)
	{
		if (!m_pending[i].exchange(false, std::memory_order_relaxed))
		{
			continue;
		}

		const Parameter& p = m_parameters[i];
		if (const ValueBuffer* vb = p.model->valueBuffer())
		{
			float prev = std::numeric_limits<float>::quiet_NaN();
			const int n = std::min(frames, vb->length());
			for (int f = 0; f < n; ++f)
			{
				const float v = fromModelValue(static_cast<int>(i), vb->value(f));
				if (v != prev)
				{
					m_plugin->queueParameterChange(p.id, v, f);
					prev = v;
				}
			}
		}
		else
		{
			m_plugin->queueParameterChange(p.id, normalizedValue(static_cast<int>(i)), 0);
		}
	}
}

void MxmPluginBridge::syncModelsFromPlugin()
{
	for (std::size_t i = 0; i < m_parameters.size(); ++i)
	{
		setModelFromNormalized(static_cast<int>(i),
			m_plugin->parameterValue(m_parameters[i].id), false);
	}
}

void MxmPluginBridge::ensureInitialized()
{
	if (!m_plugin || !m_plugin->isValid())
	{
		return;
	}

	AudioEngine* audioEngine = Engine::audioEngine();
	const double sampleRate = audioEngine ? audioEngine->outputSampleRate() : 44100.0;
	const int32_t blockSize = audioEngine ? audioEngine->framesPerPeriod() : 256;

	if (m_initialized && sampleRate == m_initializedSampleRate && blockSize == m_initializedBlockSize)
	{
		return;
	}

	if (m_initialized)
	{
		m_plugin->setActive(false);
	}

	if (!m_plugin->initialize(sampleRate, blockSize))
	{
		return;
	}

	m_initialized = true;
	m_initializedSampleRate = sampleRate;
	m_initializedBlockSize = blockSize;

	const int in = m_plugin->audioInputCount();
	const int out = m_plugin->audioOutputCount();
	m_inputBuffers.assign(static_cast<std::size_t>(in * blockSize), 0.0f);
	m_outputBuffers.assign(static_cast<std::size_t>(out * blockSize), 0.0f);
	m_inputPtrs.resize(static_cast<std::size_t>(in));
	m_outputPtrs.resize(static_cast<std::size_t>(out));
	for (int i = 0; i < in; ++i)
	{
		m_inputPtrs[static_cast<std::size_t>(i)] = m_inputBuffers.data() + i * blockSize;
	}
	for (int i = 0; i < out; ++i)
	{
		m_outputPtrs[static_cast<std::size_t>(i)] = m_outputBuffers.data() + i * blockSize;
	}

	m_plugin->setActive(true);
}

void MxmPluginBridge::copyBuffersFromLmms(const SampleFrame* buf, f_cnt_t frames)
{
	const int in = m_plugin->audioInputCount();
	if (in == 0 || !m_initialized)
	{
		return;
	}

	const int32_t blockSize = m_initializedBlockSize;
	if (in == 1)
	{
		for (f_cnt_t f = 0; f < frames; ++f)
		{
			m_inputBuffers[static_cast<std::size_t>(f)] = buf[f].left();
		}
	}
	else
	{
		for (f_cnt_t f = 0; f < frames; ++f)
		{
			m_inputBuffers[static_cast<std::size_t>(f)] = buf[f].left();
			m_inputBuffers[static_cast<std::size_t>(blockSize + f)] = buf[f].right();
		}
	}
}

void MxmPluginBridge::copyBuffersToLmms(SampleFrame* buf, f_cnt_t frames) const
{
	const int out = m_plugin->audioOutputCount();
	if (out == 0)
	{
		zeroSampleFrames(buf, frames);
		return;
	}

	const int32_t blockSize = m_initializedBlockSize;
	if (out == 1)
	{
		for (f_cnt_t f = 0; f < frames; ++f)
		{
			const float v = m_outputBuffers[static_cast<std::size_t>(f)];
			buf[f].setLeft(v);
			buf[f].setRight(v);
		}
	}
	else
	{
		for (f_cnt_t f = 0; f < frames; ++f)
		{
			buf[f].setLeft(m_outputBuffers[static_cast<std::size_t>(f)]);
			buf[f].setRight(m_outputBuffers[static_cast<std::size_t>(blockSize + f)]);
		}
	}
}

void MxmPluginBridge::run(f_cnt_t frames)
{
	if (!m_initialized)
	{
		ensureInitialized();
	}
	if (!m_initialized || !m_plugin->isValid())
	{
		return;
	}

	// Drain pending note events into the plugin.
	std::vector<bridge::NoteEvent> events;
	{
		std::lock_guard<std::mutex> lock(m_eventMutex);
		events.swap(m_pendingEvents);
	}
	for (const bridge::NoteEvent& e : events)
	{
		if (e.type == bridge::NoteEvent::Type::NoteOn)
		{
			m_plugin->noteOn(e.sampleOffset, e.channel, e.pitch, e.velocity);
		}
		else
		{
			m_plugin->noteOff(e.sampleOffset, e.channel, e.pitch, e.velocity);
		}
	}

	queueAllPendingParameters();

	bridge::ProcessContext ctx;
	Song* song = Engine::getSong();
	AudioEngine* audioEngine = Engine::audioEngine();
	ctx.sampleRate = audioEngine ? audioEngine->outputSampleRate() : 44100.0;
	if (song)
	{
		ctx.projectTimeSamples = song->getFrames();
		ctx.continousTimeSamples = song->getFrames();
		ctx.projectTimeMusic = song->getPlayPos().getTicks() / 48.0;
		ctx.barPositionMusic = song->getPlayPos().getBar()
			* (TimePos::ticksPerBar(song->getTimeSigModel()) / 48.0);
		ctx.tempo = song->getTempo();
		ctx.timeSigNumerator = song->getTimeSigModel().getNumerator();
		ctx.timeSigDenominator = song->getTimeSigModel().getDenominator();
		ctx.playing = song->isPlaying() || song->isExporting();
		ctx.recording = song->isRecording();
	}

	m_plugin->process(m_inputPtrs.empty() ? nullptr : m_inputPtrs.data(),
		m_outputPtrs.empty() ? nullptr : m_outputPtrs.data(), static_cast<int32_t>(frames), ctx);
}

void MxmPluginBridge::handleMidiEvent(const MidiEvent& event, f_cnt_t offset)
{
	if (!m_plugin || !m_plugin->isValid())
	{
		return;
	}

	bridge::NoteEvent ne;
	ne.sampleOffset = static_cast<int32_t>(offset);
	ne.channel = static_cast<int16_t>(event.channel());

	switch (event.type())
	{
	case MidiNoteOn:
		if (event.velocity() > 0)
		{
			ne.type = bridge::NoteEvent::Type::NoteOn;
			ne.pitch = static_cast<int16_t>(event.key());
			ne.velocity = event.velocity() / 127.0f;
		}
		else
		{
			ne.type = bridge::NoteEvent::Type::NoteOff;
			ne.pitch = static_cast<int16_t>(event.key());
			ne.velocity = 0.0f;
		}
		break;
	case MidiNoteOff:
		ne.type = bridge::NoteEvent::Type::NoteOff;
		ne.pitch = static_cast<int16_t>(event.key());
		ne.velocity = 0.0f;
		break;
	default:
		return;
	}

	std::lock_guard<std::mutex> lock(m_eventMutex);
	m_pendingEvents.push_back(ne);
}

//------------------------------------------------------------------------

void MxmPluginBridge::saveSettings(QDomDocument& doc, QDomElement& that)
{
	// Persist the plugin's binary state.
	QByteArray state;
	if (m_plugin->saveState(state) && !state.isEmpty())
	{
		QDomElement stateElement = doc.createElement("state");
		stateElement.setAttribute("encoding", "base64");
		stateElement.appendChild(doc.createTextNode(QString::fromLatin1(state.toBase64())));
		that.appendChild(stateElement);
	}

	// Persist the automatable parameter models (values, automation linkage).
	QDomElement models = doc.createElement("models");
	that.appendChild(models);
	for (std::size_t i = 0; i < m_parameters.size(); ++i)
	{
		m_parameters[i].model->saveSettings(doc, models,
			QStringLiteral("param%1").arg(i));
	}
}

void MxmPluginBridge::loadSettings(const QDomElement& that)
{
	m_loading = true;

	// Restore the plugin's binary state first.
	const QDomElement stateElement = that.firstChildElement("state");
	if (!stateElement.isNull())
	{
		const QByteArray state = QByteArray::fromBase64(stateElement.text().toLatin1());
		m_plugin->loadState(state);
	}

	// Restore the parameter models (values + automation).
	const QDomElement models = that.firstChildElement("models");
	if (!models.isNull())
	{
		for (std::size_t i = 0; i < m_parameters.size(); ++i)
		{
			m_parameters[i].model->loadSettings(models,
				QStringLiteral("param%1").arg(i));
		}
	}

	// Sync the models with the authoritative restored plugin state.
	syncModelsFromPlugin();

	m_loading = false;
}

//------------------------------------------------------------------------

void MxmPluginBridge::openEditor(void* windowHandle)
{
	if (m_plugin)
	{
		m_plugin->openEditor(windowHandle);
	}
}

void MxmPluginBridge::closeEditor()
{
	if (m_plugin)
	{
		m_plugin->closeEditor();
	}
}

QSize MxmPluginBridge::editorSize() const
{
	return m_plugin ? m_plugin->editorSize() : QSize(0, 0);
}

bool MxmPluginBridge::editorIsResizable() const
{
	return m_plugin && m_plugin->editorIsResizable();
}

void MxmPluginBridge::setEditorResizeCallback(std::function<void(int32_t, int32_t)> cb)
{
	if (m_plugin)
	{
		m_plugin->setEditorResizeCallback(std::move(cb));
	}
}

} // namespace mxm
