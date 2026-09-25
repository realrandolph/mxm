/*
 * Vst3Plugin.cpp - native VST3 adapter (implements the MXM plugin bridge)
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

#include "Vst3Plugin.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/connectionproxy.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/vstspeaker.h"

#include <cstdio>
#include <functional>

namespace mxm
{

using namespace Steinberg;
using namespace Steinberg::Vst;

//------------------------------------------------------------------------
// Internal host classes
//------------------------------------------------------------------------

// IComponentHandler implementation: forwards plugin-side parameter edits and
// restart requests to the LMMS side of the bridge.
//
// The reference counting is intentionally disabled (see the editorhost sample
// in the VST3 SDK): some plugins release the handler more often than they
// acquired it, which would otherwise destroy it while the host still holds a
// reference. The host owns the handler via a std::unique_ptr, so its lifetime
// is fully controlled by the host.
class Vst3ComponentHandler : public IComponentHandler
{
public:
	explicit Vst3ComponentHandler(Vst3Plugin* plugin) : m_plugin(plugin) {}
	virtual ~Vst3ComponentHandler() noexcept = default;

	tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
	tresult PLUGIN_API performEdit(ParamID id, ParamValue valueNormalized) override
	{
		if (auto* host = m_plugin->host())
		{
			host->parameterEdited(id, valueNormalized);
		}
		return kResultOk;
	}
	tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
	tresult PLUGIN_API restartComponent(int32 flags) override
	{
		if (auto* host = m_plugin->host())
		{
			host->restartRequested(flags);
		}
		return kResultOk;
	}

	tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
	{
		if (FUnknownPrivate::iidEqual(iid, IComponentHandler::iid)
			|| FUnknownPrivate::iidEqual(iid, FUnknown::iid))
		{
			*obj = this;
			return kResultOk;
		}
		*obj = nullptr;
		return kNoInterface;
	}
	uint32 PLUGIN_API addRef() override { return 1000; }
	uint32 PLUGIN_API release() override { return 1000; }

private:
	Vst3Plugin* m_plugin;
};

// IPlugFrame implementation: forwards resize requests so the host window can
// follow the plugin editor's size changes. Same non-refcounting lifetime model
// as Vst3ComponentHandler.
class Vst3PlugFrame : public IPlugFrame
{
public:
	explicit Vst3PlugFrame(Vst3Plugin* plugin) : m_plugin(plugin) {}
	virtual ~Vst3PlugFrame() noexcept = default;

	tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* newSize) override
	{
		if (!view || !newSize || m_resizing)
		{
			return kInvalidArgument;
		}

		ViewRect current{};
		auto sameSize = [](const ViewRect& lhs, const ViewRect& rhs)
		{
			return lhs.getWidth() == rhs.getWidth() && lhs.getHeight() == rhs.getHeight();
		};
		if (view->getSize(&current) == kResultTrue && sameSize(current, *newSize))
		{
			return kResultTrue;
		}

		m_resizing = true;
		m_plugin->editorResizeRequested(newSize->getWidth(), newSize->getHeight());
		m_resizing = false;

		if (view->getSize(&current) != kResultTrue || !sameSize(current, *newSize))
		{
			view->onSize(newSize);
		}
		return kResultOk;
	}

	tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
	{
		if (FUnknownPrivate::iidEqual(iid, IPlugFrame::iid)
			|| FUnknownPrivate::iidEqual(iid, FUnknown::iid))
		{
			*obj = this;
			return kResultOk;
		}
		*obj = nullptr;
		return kNoInterface;
	}
	uint32 PLUGIN_API addRef() override { return 1000; }
	uint32 PLUGIN_API release() override { return 1000; }

private:
	Vst3Plugin* m_plugin;
	bool m_resizing = false;
};

FIDString platformType()
{
#if SMTG_OS_WINDOWS
	return kPlatformTypeHWND;
#else
	return kPlatformTypeX11EmbedWindowID;
#endif
}

//------------------------------------------------------------------------
// Vst3Plugin
//------------------------------------------------------------------------

Vst3Plugin::Vst3Plugin(const std::string& modulePath, const std::string& cid)
	: m_factory(nullptr)
	, m_eventList(50)
	, m_inputParameterChanges(0)
	, m_outputParameterChanges(0)
	, m_outputEventList(50)
{
	std::string error;
	m_module = VST3::Hosting::Module::create(modulePath, error);
	if (!m_module)
	{
		return;
	}

	m_factory = m_module->getFactory();
	m_hostContext = owned(new HostApplication());
	m_componentHandler = std::make_unique<Vst3ComponentHandler>(this);
	m_plugFrame = std::make_unique<Vst3PlugFrame>(this);

	if (!instantiate(cid))
	{
		return;
	}

	m_valid = true;
}

Vst3Plugin::~Vst3Plugin()
{
	terminate();
}

void Vst3Plugin::setHost(bridge::IHost* host)
{
	m_host = host;
}

bool Vst3Plugin::instantiate(const std::string& cid)
{
	VST3::UID uid;
	bool haveUid = false;
	if (cid.empty())
	{
		// Use the first audio module class.
		for (auto& classInfo : m_factory.classInfos())
		{
			if (classInfo.category() == kVstAudioEffectClass)
			{
				uid = classInfo.ID();
				haveUid = true;
				break;
			}
		}
	}
	else
	{
		const auto uidOpt = VST3::UID::fromString(cid);
		if (!uidOpt)
		{
			return false;
		}
		uid = *uidOpt;
		haveUid = true;
	}

	if (!haveUid)
	{
		return false;
	}

	m_component = m_factory.createInstance<IComponent>(uid);
	if (!m_component)
	{
		return false;
	}

	// Metadata.
	for (auto& classInfo : m_factory.classInfos())
	{
		if (classInfo.ID() == uid)
		{
			m_name = QString::fromStdString(classInfo.name());
			m_vendor = QString::fromStdString(classInfo.vendor());
			m_version = QString::fromStdString(classInfo.version());
			m_subCategories = QString::fromStdString(classInfo.subCategoriesString());
			break;
		}
	}
	if (m_name.isEmpty())
	{
		m_name = QString::fromStdString(m_module->getName());
	}

	// Initialize the component with our host context.
	auto plugBase = U::cast<IPluginBase>(m_component);
	if (!plugBase || plugBase->initialize(m_hostContext) != kResultOk)
	{
		m_component.reset();
		return false;
	}

	m_processor = FUnknownPtr<IAudioProcessor>(m_component);
	if (!m_processor)
	{
		m_component.reset();
		return false;
	}

	// Create the controller: either the component is also the controller, or the
	// factory provides a dedicated edit controller class.
	bool isSingleComponent = false;
	if (m_component->queryInterface(IEditController::iid, (void**)&m_controller) != kResultTrue)
	{
		TUID controllerCID;
		if (m_component->getControllerClassId(controllerCID) == kResultTrue)
		{
			m_controller = m_factory.createInstance<IEditController>(VST3::UID(controllerCID));
			if (m_controller)
			{
				auto ctrlBase = U::cast<IPluginBase>(m_controller);
				if (!ctrlBase || ctrlBase->initialize(m_hostContext) != kResultOk)
				{
					m_controller.reset();
					return false;
				}
			}
		}
	}
	else
	{
		isSingleComponent = true;
	}

	if (!m_controller)
	{
		return false;
	}

	if (!isSingleComponent)
	{
		connectComponents();
	}

	m_controller->setComponentHandler(m_componentHandler.get());

	// Determine whether the plugin accepts MIDI/note input.
	m_hasEventInput = m_component->getBusCount(MediaTypes::kEvent, BusDirections::kInput) > 0;

	return true;
}

bool Vst3Plugin::connectComponents()
{
	auto compICP = U::cast<IConnectionPoint>(m_component);
	auto contrICP = U::cast<IConnectionPoint>(m_controller);
	if (!compICP || !contrICP)
	{
		return false;
	}

	m_componentCP = owned(new ConnectionProxy(compICP));
	m_controllerCP = owned(new ConnectionProxy(contrICP));

	return m_componentCP->connect(contrICP) == kResultTrue
		&& m_controllerCP->connect(compICP) == kResultTrue;
}

void Vst3Plugin::disconnectComponents()
{
	if (m_componentCP)
	{
		m_componentCP->disconnect();
		m_componentCP.reset();
	}
	if (m_controllerCP)
	{
		m_controllerCP->disconnect();
		m_controllerCP.reset();
	}
}

bool Vst3Plugin::hasEditor() const
{
	return m_controller != nullptr;
}

//------------------------------------------------------------------------

bool Vst3Plugin::initialize(double sampleRate, int32_t blockSize)
{
	if (!m_valid)
	{
		return false;
	}

	if (!setupBusLayout())
	{
		return false;
	}

	ProcessSetup setup{kRealtime, kSample32, blockSize, sampleRate};
	if (m_processor->setupProcessing(setup) != kResultOk)
	{
		return false;
	}

	m_processData.prepare(*m_component, 0, kSample32);
	m_processData.inputEvents = &m_eventList;
	m_processData.inputParameterChanges = &m_inputParameterChanges;
	m_processData.outputParameterChanges = &m_outputParameterChanges;
	m_processData.outputEvents = &m_outputEventList;
	m_processData.processContext = &m_processContext;
	m_inputParameterChanges.setMaxParameters(1000);
	m_outputParameterChanges.setMaxParameters(1000);

	return true;
}

bool Vst3Plugin::setupBusLayout()
{
	m_numInputChannels = 0;
	m_numOutputChannels = 0;

	const int inBusCount = m_component->getBusCount(MediaTypes::kAudio, BusDirections::kInput);
	const int outBusCount = m_component->getBusCount(MediaTypes::kAudio, BusDirections::kOutput);

	int inChannels = 0;
	int outChannels = 0;
	if (inBusCount > 0)
	{
		BusInfo info;
		if (m_component->getBusInfo(MediaTypes::kAudio, BusDirections::kInput, 0, info) == kResultOk)
		{
			inChannels = info.channelCount;
		}
	}
	if (outBusCount > 0)
	{
		BusInfo info;
		if (m_component->getBusInfo(MediaTypes::kAudio, BusDirections::kOutput, 0, info) == kResultOk)
		{
			outChannels = info.channelCount;
		}
	}

	// MXM is a stereo host. If the plugin's main bus is mono, ask it to run with
	// a stereo arrangement so a single instance covers both channels.
	const bool wantStereoIn = inBusCount > 0 && inChannels < 2;
	const bool wantStereoOut = outChannels < 2;
	if (wantStereoIn || wantStereoOut)
	{
		const int maxIn = std::max(1, inBusCount);
		const int maxOut = std::max(1, outBusCount);
		std::vector<SpeakerArrangement> inputs(maxIn, SpeakerArr::kEmpty);
		std::vector<SpeakerArrangement> outputs(maxOut, SpeakerArr::kEmpty);
		if (inBusCount > 0)
		{
			inputs[0] = SpeakerArr::kStereo;
		}
		if (outBusCount > 0)
		{
			outputs[0] = SpeakerArr::kStereo;
		}
		if (m_processor->setBusArrangements(inputs.data(), maxIn, outputs.data(), maxOut) == kResultOk)
		{
			if (inBusCount > 0)
			{
				BusInfo info;
				if (m_component->getBusInfo(MediaTypes::kAudio, BusDirections::kInput, 0, info) == kResultOk)
				{
					inChannels = info.channelCount;
				}
			}
			if (outBusCount > 0)
			{
				BusInfo info;
				if (m_component->getBusInfo(MediaTypes::kAudio, BusDirections::kOutput, 0, info) == kResultOk)
				{
					outChannels = info.channelCount;
				}
			}
		}
	}

	// Activate the main input/output audio buses and the main event input bus.
	if (inBusCount > 0)
	{
		m_component->activateBus(MediaTypes::kAudio, BusDirections::kInput, 0, true);
	}
	if (outBusCount > 0)
	{
		m_component->activateBus(MediaTypes::kAudio, BusDirections::kOutput, 0, true);
	}
	if (m_hasEventInput)
	{
		m_component->activateBus(MediaTypes::kEvent, BusDirections::kInput, 0, true);
	}

	m_numInputChannels = inChannels;
	m_numOutputChannels = outChannels;
	return outChannels > 0;
}

void Vst3Plugin::setActive(bool active)
{
	if (!m_valid)
	{
		return;
	}

	if (active)
	{
		if (!m_active)
		{
			m_component->setActive(true);
			m_processor->setProcessing(true);
			m_active = true;
		}
	}
	else if (m_active)
	{
		m_processor->setProcessing(false);
		m_component->setActive(false);
		m_active = false;
	}
}

void Vst3Plugin::terminate()
{
	closeEditor();

	if (m_active)
	{
		m_processor->setProcessing(false);
		m_component->setActive(false);
		m_active = false;
	}

	disconnectComponents();

	// Detach the component handler before terminating the controller. Some
	// plugins (notably JUCE-based ones) release the handler during their own
	// terminate()/destructor; clearing it here keeps the reference count
	// balanced so the handler survives until we release it below.
	if (m_controller)
	{
		m_controller->setComponentHandler(nullptr);
	}

	bool controllerIsComponent = false;
	if (m_component)
	{
		controllerIsComponent = FUnknownPtr<IEditController>(m_component).getInterface() != nullptr;
		if (auto plugBase = U::cast<IPluginBase>(m_component))
		{
			plugBase->terminate();
		}
	}
	if (m_controller && !controllerIsComponent)
	{
		if (auto ctrlBase = U::cast<IPluginBase>(m_controller))
		{
			ctrlBase->terminate();
		}
	}

	// Release all references to the plugin while its module is still loaded.
	// The module itself (m_module) is intentionally NOT released here: it is
	// owned by m_factory as well, and the member destruction order (m_factory
	// before m_module) guarantees the factory is released before the module is
	// unloaded.
	m_controller.reset();
	m_processor.reset();
	m_component.reset();
	m_componentCP.reset();
	m_controllerCP.reset();
	m_plugView.reset();
	m_valid = false;
}

//------------------------------------------------------------------------

void Vst3Plugin::process(float* const* inputs, float* const* outputs,
	int32_t numSamples, const bridge::ProcessContext& ctx)
{
	if (!m_valid || !m_active)
	{
		return;
	}

	m_processData.numSamples = numSamples;

	// Point the input/output bus buffers at the (deinterleaved) buffers passed
	// by the bridge.
	for (int32_t b = 0; b < m_processData.numInputs; ++b)
	{
		AudioBusBuffers& bus = m_processData.inputs[b];
		for (int32_t ch = 0; ch < bus.numChannels; ++ch)
		{
			bus.channelBuffers32[ch] = nullptr;
		}
	}
	int32_t ch = 0;
	for (int32_t b = 0; b < m_processData.numInputs && ch < m_numInputChannels; ++b)
	{
		AudioBusBuffers& bus = m_processData.inputs[b];
		for (int32_t c = 0; c < bus.numChannels && ch < m_numInputChannels; ++c, ++ch)
		{
			bus.channelBuffers32[c] = inputs ? inputs[ch] : nullptr;
		}
	}
	ch = 0;
	for (int32_t b = 0; b < m_processData.numOutputs && ch < m_numOutputChannels; ++b)
	{
		AudioBusBuffers& bus = m_processData.outputs[b];
		for (int32_t c = 0; c < bus.numChannels && ch < m_numOutputChannels; ++c, ++ch)
		{
			bus.channelBuffers32[c] = outputs[ch];
		}
	}

	// Fill the process context (transport/process information).
	m_processContext = {};
	m_processContext.sampleRate = ctx.sampleRate;
	m_processContext.projectTimeSamples = ctx.projectTimeSamples;
	m_processContext.continousTimeSamples = ctx.continousTimeSamples;
	m_processContext.state |= ProcessContext::kContTimeValid;
	m_processContext.projectTimeMusic = ctx.projectTimeMusic;
	m_processContext.state |= ProcessContext::kProjectTimeMusicValid;
	m_processContext.barPositionMusic = ctx.barPositionMusic;
	m_processContext.state |= ProcessContext::kBarPositionValid;
	m_processContext.tempo = ctx.tempo;
	m_processContext.state |= ProcessContext::kTempoValid;
	m_processContext.timeSigNumerator = ctx.timeSigNumerator;
	m_processContext.timeSigDenominator = ctx.timeSigDenominator;
	m_processContext.state |= ProcessContext::kTimeSigValid;
	if (ctx.playing)
	{
		m_processContext.state |= ProcessContext::kPlaying;
	}
	if (ctx.recording)
	{
		m_processContext.state |= ProcessContext::kRecording;
	}
	if (ctx.cycleActive)
	{
		m_processContext.cycleStartMusic = ctx.cycleStartMusic;
		m_processContext.cycleEndMusic = ctx.cycleEndMusic;
		m_processContext.state |= ProcessContext::kCycleActive | ProcessContext::kCycleValid;
	}

	m_processor->process(m_processData);

	// Reset the event and parameter queues for the next block.
	m_eventList.clear();
	m_outputEventList.clear();
	m_inputParameterChanges.clearQueue();
	m_outputParameterChanges.clearQueue();
}

void Vst3Plugin::noteOn(int32_t sampleOffset, int16_t channel, int16_t pitch, float velocity)
{
	Event e{};
	e.busIndex = 0;
	e.sampleOffset = sampleOffset;
	e.ppqPosition = 0.0;
	e.flags = Event::kIsLive;
	e.type = Event::kNoteOnEvent;
	e.noteOn.channel = channel;
	e.noteOn.pitch = pitch;
	e.noteOn.velocity = velocity;
	e.noteOn.length = 0;
	e.noteOn.tuning = 0.0f;
	e.noteOn.noteId = -1;
	m_eventList.addEvent(e);
}

void Vst3Plugin::noteOff(int32_t sampleOffset, int16_t channel, int16_t pitch, float velocity)
{
	Event e{};
	e.busIndex = 0;
	e.sampleOffset = sampleOffset;
	e.ppqPosition = 0.0;
	e.flags = Event::kIsLive;
	e.type = Event::kNoteOffEvent;
	e.noteOff.channel = channel;
	e.noteOff.pitch = pitch;
	e.noteOff.velocity = velocity;
	e.noteOff.noteId = -1;
	m_eventList.addEvent(e);
}

void Vst3Plugin::queueParameterChange(uint32_t id, float valueNormalized, int32_t sampleOffset)
{
	int32_t index = 0;
	if (auto* queue = m_inputParameterChanges.addParameterData(id, index))
	{
		queue->addPoint(sampleOffset, valueNormalized, index);
	}
}

//------------------------------------------------------------------------

int32_t Vst3Plugin::parameterCount() const
{
	if (m_controller)
	{
		return m_controller->getParameterCount();
	}
	return 0;
}

bool Vst3Plugin::parameterInfo(int32_t index, bridge::ParameterInfo& out) const
{
	if (!m_controller)
	{
		return false;
	}

	ParameterInfo info;
	if (m_controller->getParameterInfo(index, info) != kResultOk)
	{
		return false;
	}

	out.id = info.id;
	out.name = QString::fromStdString(Steinberg::Vst::StringConvert::convert(info.title));
	out.shortName = QString::fromStdString(Steinberg::Vst::StringConvert::convert(info.shortTitle));
	out.unit = QString::fromStdString(Steinberg::Vst::StringConvert::convert(info.units));
	out.defaultNormalizedValue = info.defaultNormalizedValue;
	out.stepCount = info.stepCount;
	out.flags = info.flags;
	return true;
}

float Vst3Plugin::parameterValue(uint32_t id) const
{
	if (m_controller)
	{
		return m_controller->getParamNormalized(id);
	}
	return 0.0f;
}

void Vst3Plugin::setParameterValue(uint32_t id, float valueNormalized)
{
	if (m_controller)
	{
		m_controller->setParamNormalized(id, valueNormalized);
	}
}

//------------------------------------------------------------------------

bool Vst3Plugin::saveState(QByteArray& out)
{
	if (!m_component)
	{
		return false;
	}

	MemoryStream stream;
	if (m_component->getState(&stream) != kResultOk)
	{
		return false;
	}

	const TSize size = stream.getSize();
	if (size > 0)
	{
		out = QByteArray(stream.getData(), static_cast<int>(size));
		return true;
	}
	return false;
}

bool Vst3Plugin::loadState(const QByteArray& in)
{
	if (!m_component || in.isEmpty())
	{
		return false;
	}

	MemoryStream stream(const_cast<char*>(in.constData()), in.size());
	if (m_component->setState(&stream) != kResultOk)
	{
		return false;
	}

	// Sync the controller (and thus the GUI) with the restored state.
	if (m_controller)
	{
		MemoryStream ctrlStream(const_cast<char*>(in.constData()), in.size());
		m_controller->setComponentState(&ctrlStream);
	}

	return true;
}

//------------------------------------------------------------------------

bool Vst3Plugin::openEditor(void* parentWindowHandle)
{
	if (!m_controller)
	{
		return false;
	}

	if (!m_plugView)
	{
		m_plugView = owned(m_controller->createView(ViewType::kEditor));
		if (!m_plugView)
		{
			return false;
		}
	}

	m_plugView->setFrame(m_plugFrame.get());

	if (m_plugView->attached(parentWindowHandle, platformType()) != kResultTrue)
	{
		m_plugView->setFrame(nullptr);
		return false;
	}

	return true;
}

void Vst3Plugin::closeEditor()
{
	if (m_plugView)
	{
		m_plugView->setFrame(nullptr);
		m_plugView->removed();
		m_plugView.reset();
	}
}

QSize Vst3Plugin::editorSize() const
{
	if (m_plugView)
	{
		ViewRect rect{};
		if (m_plugView->getSize(&rect) == kResultTrue)
		{
			return QSize(rect.getWidth(), rect.getHeight());
		}
	}
	return QSize(0, 0);
}

bool Vst3Plugin::editorIsResizable() const
{
	if (m_plugView)
	{
		return m_plugView->canResize() == kResultTrue;
	}
	return false;
}

QSize Vst3Plugin::resizeEditor(const QSize& size)
{
	if (!m_plugView || size.width() <= 0 || size.height() <= 0)
	{
		return QSize();
	}

	ViewRect rect(0, 0, size.width(), size.height());
	if (m_plugView->canResize() == kResultTrue)
	{
		m_plugView->checkSizeConstraint(&rect);
	}
	m_plugView->onSize(&rect);
	return QSize(rect.getWidth(), rect.getHeight());
}

} // namespace mxm
