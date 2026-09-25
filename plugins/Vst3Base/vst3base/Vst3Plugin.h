/*
 * Vst3Plugin.h - native VST3 adapter (implements the MXM plugin bridge)
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

#ifndef MXM_VST3_PLUGIN_H
#define MXM_VST3_PLUGIN_H

#include "MxmBridge.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "public.sdk/source/vst/hosting/connectionproxy.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/gui/iplugview.h"

namespace mxm
{

class Vst3ComponentHandler;
class Vst3PlugFrame;

//! Native VST3 adapter: translates the VST3 model into the MXM plugin bridge.
class Vst3Plugin : public bridge::IPlugin
{
public:
	//! @param modulePath path to the .vst3 bundle.
	//! @param cid        class ID of the audio module class to instantiate.
	Vst3Plugin(const std::string& modulePath, const std::string& cid);
	~Vst3Plugin() override;

	Vst3Plugin(const Vst3Plugin&) = delete;
	Vst3Plugin& operator=(const Vst3Plugin&) = delete;

	// --- bridge::IPlugin ---
	void setHost(bridge::IHost* host) override;

	//! Host callbacks (used by the internal host classes).
	bridge::IHost* host() const { return m_host; }
	//! Install a callback invoked when the plugin editor requests a resize.
	void setEditorResizeCallback(std::function<void(int32_t, int32_t)> cb) override
	{
		m_editorResizeCallback = std::move(cb);
	}
	//! Forward a resize request from the plugin's IPlugFrame.
	void editorResizeRequested(int32_t width, int32_t height)
	{
		if (m_editorResizeCallback) { m_editorResizeCallback(width, height); }
	}

	bool isValid() const override { return m_valid; }
	QString name() const override { return m_name; }
	QString vendor() const override { return m_vendor; }
	QString version() const override { return m_version; }
	QString subCategories() const override { return m_subCategories; }
	bool hasEditor() const override;

	bool initialize(double sampleRate, int32_t blockSize) override;
	void setActive(bool active) override;
	void terminate() override;

	int audioInputCount() const override { return m_numInputChannels; }
	int audioOutputCount() const override { return m_numOutputChannels; }
	bool hasEventInput() const override { return m_hasEventInput; }

	void process(float* const* inputs, float* const* outputs,
		int32_t numSamples, const bridge::ProcessContext& ctx) override;

	void noteOn(int32_t sampleOffset, int16_t channel, int16_t pitch, float velocity) override;
	void noteOff(int32_t sampleOffset, int16_t channel, int16_t pitch, float velocity) override;
	void queueParameterChange(uint32_t id, float valueNormalized, int32_t sampleOffset) override;

	int32_t parameterCount() const override;
	bool parameterInfo(int32_t index, bridge::ParameterInfo& out) const override;
	float parameterValue(uint32_t id) const override;
	void setParameterValue(uint32_t id, float valueNormalized) override;

	bool saveState(QByteArray& out) override;
	bool loadState(const QByteArray& in) override;

	bool openEditor(void* parentWindowHandle) override;
	void closeEditor() override;
	QSize editorSize() const override;
	bool editorIsResizable() const override;

private:
	bool instantiate(const std::string& cid);
	bool connectComponents();
	void disconnectComponents();
	bool setupBusLayout();

	bool m_valid = false;
	QString m_name;
	QString m_vendor;
	QString m_version;
	QString m_subCategories;

	VST3::Hosting::Module::Ptr m_module;
	VST3::Hosting::PluginFactory m_factory;

	Steinberg::IPtr<Steinberg::Vst::IComponent> m_component;
	Steinberg::IPtr<Steinberg::Vst::IEditController> m_controller;
	Steinberg::IPtr<Steinberg::Vst::IAudioProcessor> m_processor;

	Steinberg::IPtr<Steinberg::Vst::HostApplication> m_hostContext;
	std::unique_ptr<Vst3ComponentHandler> m_componentHandler;
	std::unique_ptr<Vst3PlugFrame> m_plugFrame;

	Steinberg::IPtr<Steinberg::Vst::ConnectionProxy> m_componentCP;
	Steinberg::IPtr<Steinberg::Vst::ConnectionProxy> m_controllerCP;

	Steinberg::Vst::HostProcessData m_processData;
	Steinberg::Vst::EventList m_eventList;
	Steinberg::Vst::ParameterChanges m_inputParameterChanges;
	Steinberg::Vst::ParameterChanges m_outputParameterChanges;
	Steinberg::Vst::EventList m_outputEventList;
	Steinberg::Vst::ProcessContext m_processContext;

	Steinberg::IPtr<Steinberg::IPlugView> m_plugView;

	int32_t m_numInputChannels = 0;
	int32_t m_numOutputChannels = 0;
	bool m_hasEventInput = false;
	bool m_active = false;
	bool m_processing = false;

	bridge::IHost* m_host = nullptr;

	std::function<void(int32_t, int32_t)> m_editorResizeCallback;

	mutable std::vector<Steinberg::Vst::ParameterInfo> m_parameterInfos;
};

} // namespace mxm

#endif // MXM_VST3_PLUGIN_H
