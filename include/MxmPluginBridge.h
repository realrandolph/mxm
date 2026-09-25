/*
 * MxmPluginBridge.h - format-neutral LMMS-side mapping of the plugin bridge
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

#ifndef MXM_PLUGIN_BRIDGE_H
#define MXM_PLUGIN_BRIDGE_H

#include "MxmBridge.h"

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include <QSize>
#include <QString>

#include "AutomatableModel.h"
#include "MxmTypes.h"
#include "mxm_export.h"

class QDomDocument;
class QDomElement;

namespace mxm
{

class MidiEvent;
class Model;
class SampleFrame;

/**
	Format-neutral mapping of the plugin bridge into MXM's plugin environment.

	This class owns a single `bridge::IPlugin` (a format adapter) and exposes the
	subset of it that MXM understands: automatable parameters, audio processing,
	note/event input, state save/restore and editor lifetime. It contains no
	format-specific code, so CLAP and other adapters reuse it unchanged.
*/
class MXM_EXPORT MxmPluginBridge : public bridge::IHost
{
public:
	//! @param meAsModel the owning Effect/Instrument (used as model parent).
	MxmPluginBridge(Model* meAsModel, std::unique_ptr<bridge::IPlugin> plugin);
	~MxmPluginBridge();

	MxmPluginBridge(const MxmPluginBridge&) = delete;
	MxmPluginBridge& operator=(const MxmPluginBridge&) = delete;

	//! Create the parameter automatable models. Must be called once.
	void init();

	bridge::IPlugin* plugin() { return m_plugin.get(); }
	const bridge::IPlugin* plugin() const { return m_plugin.get(); }
	bool isValid() const { return m_plugin && m_plugin->isValid(); }
	bool hasGui() const { return m_plugin && m_plugin->hasEditor(); }

	//! Number of exposed (non-hidden, automatable) parameters.
	int parameterModelCount() const { return static_cast<int>(m_parameters.size()); }
	AutomatableModel* parameterModel(int index) const;
	QString parameterName(int index) const;
	uint32_t parameterId(int index) const;

	// --- audio thread helpers ---
	//! Copy an interleaved MXM buffer into the plugin's deinterleaved inputs.
	void copyBuffersFromLmms(const SampleFrame* buf, f_cnt_t frames);
	//! Copy the plugin's deinterleaved outputs into an interleaved MXM buffer.
	void copyBuffersToLmms(SampleFrame* buf, f_cnt_t frames) const;
	//! Queue pending parameter changes and run the plugin for @p frames frames.
	void run(f_cnt_t frames);
	//! Note/event input (may be called from any thread).
	void handleMidiEvent(const MidiEvent& event, f_cnt_t offset);

	// --- state ---
	void saveSettings(QDomDocument& doc, QDomElement& that);
	void loadSettings(const QDomElement& that);

	// --- editor (GUI thread) ---
	void openEditor(void* windowHandle);
	void closeEditor();
	QSize editorSize() const;
	bool editorIsResizable() const;
	void setEditorResizeCallback(std::function<void(int32_t, int32_t)> cb);

	// --- bridge::IHost (called from the plugin's own editor / UI thread) ---
	void parameterEdited(uint32_t id, float valueNormalized) override;
	void restartRequested(int32_t flags) override;

private:
	void ensureInitialized();
	void createParameterModels();
	void onParameterModelChanged(int index);
	void queueAllPendingParameters();
	void syncModelsFromPlugin();
	float normalizedValue(int index) const;
	float fromModelValue(int index, float modelValue) const;
	void setModelFromNormalized(int index, float normalized, bool markPending);

	struct Parameter
	{
		uint32_t id = 0;
		int32_t stepCount = 0;
		AutomatableModel* model = nullptr;
	};

	Model* m_meAsModel;
	std::unique_ptr<bridge::IPlugin> m_plugin;
	std::vector<Parameter> m_parameters;

	std::deque<std::atomic<bool>> m_pending;

	std::vector<float> m_inputBuffers;   //!< deinterleaved input (inputChannels * blockSize)
	std::vector<float> m_outputBuffers;  //!< deinterleaved output (outputChannels * blockSize)
	std::vector<float*> m_inputPtrs;
	std::vector<float*> m_outputPtrs;

	// Note/event queue (thread-safe: written from any thread, drained by the
	// audio thread inside run()).
	std::mutex m_eventMutex;
	std::vector<bridge::NoteEvent> m_pendingEvents;

	bool m_initialized = false;
	double m_initializedSampleRate = 0.0;
	int32_t m_initializedBlockSize = 0;
	bool m_loading = false;
	std::atomic<bool> m_syncingFromPlugin{false};
};

} // namespace mxm

#endif // MXM_PLUGIN_BRIDGE_H
