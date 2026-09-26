/*
 * MxmBridge.h - format-neutral plugin bridge interface
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

#ifndef MXM_BRIDGE_H
#define MXM_BRIDGE_H

#include <QByteArray>
#include <QSize>
#include <QString>

#include <functional>

namespace mxm
{

/**
	Format-neutral plugin bridge.

	The MXM plugin bridge is the small translation boundary between MXM's own
	plugin environment and a modern plugin format. A format adapter (e.g. the
	VST3 adapter) implements `bridge::IPlugin` and translates its native model
	into the subset of concepts every modern plugin host has to move around:
	audio ports, note/events, parameters, state, transport/process information,
	GUI lifetime and capabilities.

	Nothing in this header refers to a concrete plugin format, so future
	adapters (CLAP, ...) reuse the exact same bridge and the LMMS-side mapping
	in MxmPluginBridge.
*/
namespace bridge
{

//! Description of a single parameter.
struct ParameterInfo
{
	uint32_t id = 0;
	QString name;
	QString shortName;
	QString unit;
	float defaultNormalizedValue = 0.0f;
	int32_t stepCount = 0;   //!< 0 = continuous, 1 = toggle, >1 = discrete
	int32_t flags = 0;

	static constexpr int32_t CanAutomate = 1 << 0;
	static constexpr int32_t IsReadOnly = 1 << 1;
	static constexpr int32_t IsHidden = 1 << 4;
	static constexpr int32_t IsProgramChange = 1 << 15;
	static constexpr int32_t IsBypass = 1 << 16;
};

//! A note/event to be delivered to the plugin.
struct NoteEvent
{
	enum class Type : uint8_t { NoteOn, NoteOff };

	Type type = Type::NoteOn;
	int32_t sampleOffset = 0;  //!< offset within the current processing block
	int16_t channel = 0;       //!< channel within the event bus
	int16_t pitch = 60;
	float velocity = 1.0f;     //!< normalized [0, 1]
	int32_t noteId = -1;
};

//! Transport/process information supplied with each processing call.
struct ProcessContext
{
	double sampleRate = 44100.0;
	int64_t projectTimeSamples = 0;    //!< always valid
	int64_t continousTimeSamples = 0;  //!< project time without loop (optional)
	double projectTimeMusic = 0.0;     //!< musical position in quarter notes
	double barPositionMusic = 0.0;     //!< last bar start position (quarter notes)
	double cycleStartMusic = 0.0;
	double cycleEndMusic = 0.0;
	double tempo = 120.0;
	int32_t timeSigNumerator = 4;
	int32_t timeSigDenominator = 4;
	bool playing = false;
	bool cycleActive = false;
	bool recording = false;
};

//! Host callbacks delivered to the LMMS side of the bridge (UI thread).
class IHost
{
public:
	virtual ~IHost() = default;

	//! Called when the plugin's own editor edits a parameter.
	virtual void parameterEdited(uint32_t id, float valueNormalized) = 0;
	//! Called when the plugin requests a component restart.
	virtual void restartRequested(int32_t flags) = 0;
};

//! The format-neutral plugin interface, implemented by format adapters.
class IPlugin
{
public:
	virtual ~IPlugin() = default;

	virtual void setHost(IHost* host) = 0;

	virtual bool isValid() const = 0;
	virtual QString name() const = 0;
	virtual QString vendor() const = 0;
	virtual QString version() const = 0;
	virtual QString subCategories() const = 0;
	virtual bool hasEditor() const = 0;

	// --- lifecycle (UI thread) ---
	//! Setup processing for the given sample rate and block size and activate.
	virtual bool initialize(double sampleRate, int32_t blockSize) = 0;
	//! Suspend/resume processing.
	virtual void setActive(bool active) = 0;
	//! Tear down the plugin and release all resources.
	virtual void terminate() = 0;

	// --- audio ports (valid after initialize()) ---
	virtual int audioInputCount() const = 0;   //!< 0 for instruments
	virtual int audioOutputCount() const = 0;
	virtual bool hasEventInput() const = 0;

	// --- audio processing (audio thread) ---
	virtual void process(float* const* inputs, float* const* outputs,
		int32_t numSamples, const ProcessContext& ctx) = 0;

	// --- events / parameter queues (audio thread) ---
	virtual void noteOn(int32_t sampleOffset, int16_t channel, int16_t pitch, float velocity) = 0;
	virtual void noteOff(int32_t sampleOffset, int16_t channel, int16_t pitch, float velocity) = 0;
	virtual void queueParameterChange(uint32_t id, float valueNormalized, int32_t sampleOffset) = 0;

	// --- parameters (UI thread) ---
	virtual int32_t parameterCount() const = 0;
	virtual bool parameterInfo(int32_t index, ParameterInfo& out) const = 0;
	virtual float parameterValue(uint32_t id) const = 0;
	//! Deliver a parameter change immediately (used while transport is stopped).
	virtual void setParameterValue(uint32_t id, float valueNormalized) = 0;

	// --- state (UI thread) ---
	virtual bool saveState(QByteArray& out) = 0;
	virtual bool loadState(const QByteArray& in) = 0;

	// --- editor (UI thread) ---
	//! @param parentWindowHandle native window handle (HWND / X11 Window id).
	virtual bool openEditor(void* parentWindowHandle) = 0;
	virtual void closeEditor() = 0;
	virtual QSize editorSize() const = 0;
	virtual bool editorIsResizable() const = 0;
	virtual QSize resizeEditor(const QSize& size) = 0;
	//! Install a callback invoked when the plugin editor requests a resize.
	virtual void setEditorResizeCallback(std::function<void(int32_t, int32_t)> cb) = 0;
};

} // namespace bridge
} // namespace mxm

#endif // MXM_BRIDGE_H
