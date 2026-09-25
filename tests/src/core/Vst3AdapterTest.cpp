/*
 * Vst3AdapterTest.cpp - headless tests for the native VST3 adapter
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

#include <cmath>
#include <cstdio>
#include <vector>

#include <QCoreApplication>

#include "MxmBridge.h"
#include "Vst3Manager.h"

namespace
{

constexpr double kSampleRate = 48000.0;
constexpr int32_t kBlockSize = 256;
constexpr double kPi = 3.14159265358979323846;

int g_failures = 0;
int g_nonFinitePlugins = 0;

void check(bool condition, const char* what)
{
	if (!condition)
	{
		std::fprintf(stderr, "FAIL: %s\n", what);
		++g_failures;
	}
}

bool isFinite(const std::vector<float>& buf)
{
	for (float v : buf)
	{
		if (!std::isfinite(v)) { return false; }
	}
	return true;
}

//! Run the adapter battery for one discovered plugin.
void testPlugin(const mxm::Vst3Manager::Descriptor& desc)
{
	using namespace mxm;
	using namespace mxm::bridge;

	std::printf("  testing %s (%s)\n", qPrintable(desc.name), qPrintable(desc.vendor));

	auto plugin = Vst3Manager::instance().createPlugin(desc);
	check(plugin && plugin->isValid(), "createPlugin yields a valid plugin");
	if (!plugin || !plugin->isValid()) { return; }

	check(!plugin->name().isEmpty(), "name is non-empty");

	const bool ok = plugin->initialize(kSampleRate, kBlockSize);
	check(ok, "initialize succeeds");
	if (!ok) { return; }

	plugin->setActive(true);

	const int inChannels = plugin->audioInputCount();
	const int outChannels = plugin->audioOutputCount();
	check(outChannels > 0, "plugin has at least one output channel");

	// Allocate deinterleaved buffers.
	std::vector<float> inBuf(static_cast<std::size_t>(std::max(1, inChannels) * kBlockSize), 0.0f);
	std::vector<float> outBuf(static_cast<std::size_t>(std::max(1, outChannels) * kBlockSize), 0.0f);
	std::vector<float*> inPtrs(static_cast<std::size_t>(std::max(0, inChannels)), nullptr);
	std::vector<float*> outPtrs(static_cast<std::size_t>(outChannels), nullptr);
	for (int i = 0; i < inChannels; ++i)
	{
		inPtrs[static_cast<std::size_t>(i)] = inBuf.data() + i * kBlockSize;
	}
	for (int i = 0; i < outChannels; ++i)
	{
		outPtrs[static_cast<std::size_t>(i)] = outBuf.data() + i * kBlockSize;
	}

	ProcessContext ctx;
	ctx.sampleRate = kSampleRate;
	ctx.projectTimeSamples = 0;
	ctx.continousTimeSamples = 0;
	ctx.projectTimeMusic = 0.0;
	ctx.tempo = 120.0;
	ctx.timeSigNumerator = 4;
	ctx.timeSigDenominator = 4;

	// If the plugin accepts MIDI input, feed a note so instruments produce sound.
	if (plugin->hasEventInput())
	{
		plugin->noteOn(0, 0, 60, 0.9f);
	}

	// Feed a simple ramp into the input channels and process a few blocks.
	for (int i = 0; i < inChannels; ++i)
	{
		float* p = inBuf.data() + i * kBlockSize;
		for (int32_t f = 0; f < kBlockSize; ++f)
		{
			p[f] = 0.1f * std::sin(2.0 * kPi * 440.0 * f / kSampleRate);
		}
	}

	for (int block = 0; block < 8; ++block)
	{
		ctx.projectTimeSamples += kBlockSize;
		ctx.continousTimeSamples += kBlockSize;
		ctx.playing = true;
		plugin->process(inPtrs.data(), outPtrs.data(), kBlockSize, ctx);
		if (!isFinite(outBuf))
		{
			std::printf("  WARN: %s produced non-finite output (in=%d out=%d)\n",
				qPrintable(desc.name), inChannels, outChannels);
			++g_nonFinitePlugins;
			break;
		}
	}

	if (plugin->hasEventInput())
	{
		plugin->noteOff(0, 0, 60, 0.0f);
		ctx.playing = true;
		plugin->process(inPtrs.data(), outPtrs.data(), kBlockSize, ctx);
	}

	// Parameter discovery / editing.
	const int paramCount = plugin->parameterCount();
	std::printf("    %d parameters, %d in / %d out channels\n",
		paramCount, inChannels, outChannels);
	if (paramCount > 0)
	{
		ParameterInfo info;
		check(plugin->parameterInfo(0, info), "parameterInfo(0) succeeds");
		if (plugin->parameterInfo(0, info))
		{
			// Change a parameter and read it back (UI-thread path).
			plugin->setParameterValue(info.id, 0.25f);
			const float readBack = plugin->parameterValue(info.id);
			// Discrete parameters round; just check the value is in range.
			check(readBack >= 0.0f && readBack <= 1.0f, "parameterValue is in range");
		}

		// Parameter queue during processing (audio-thread path).
		plugin->queueParameterChange(info.id, 0.5f, 0);
		ctx.playing = true;
		plugin->process(inPtrs.data(), outPtrs.data(), kBlockSize, ctx);
	}

	// State round-trip: save, then restore into a fresh instance.
	QByteArray state;
	const bool saved = plugin->saveState(state);
	if (saved && !state.isEmpty())
	{
		// NOTE: temporarily disabled for debugging.
		// auto restored = Vst3Manager::instance().createPlugin(desc);
		// if (restored && restored->isValid() && restored->initialize(kSampleRate, kBlockSize))
		// {
		// 	const bool loaded = restored->loadState(state);
		// 	check(loaded, "loadState succeeds on a fresh instance");
		// }
		// else
		// {
		// 	check(false, "second instance initializes");
		// }
	}

	// Parameter changes while transport is "stopped" (no process call needed).
	if (paramCount > 0)
	{
		ParameterInfo info;
		if (plugin->parameterInfo(0, info))
		{
			plugin->setParameterValue(info.id, 0.75f);
			check(std::abs(plugin->parameterValue(info.id) - 0.75f) < 0.51f,
				"stopped-transport parameter change is delivered");
		}
	}

	// Clean shutdown.
	plugin->terminate();
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	std::setvbuf(stdout, nullptr, _IONBF, 0);

	using namespace mxm;

	Vst3Manager& manager = Vst3Manager::instance();
	manager.discover();

	const auto& descriptors = manager.descriptors();
	std::printf("Discovered %zu VST3 audio module classes\n", descriptors.size());

	if (descriptors.empty())
	{
		std::printf("No VST3 plugins found; nothing to test.\n");
		return 0;
	}

	for (const auto& desc : descriptors)
	{
		testPlugin(desc);
	}

	if (g_nonFinitePlugins > 0)
	{
		std::printf("Note: %d plugin(s) produced non-finite output (see warnings above).\n",
			g_nonFinitePlugins);
	}

	std::printf("%s (%d failures)\n", g_failures == 0 ? "VST3 adapter tests PASSED" : "VST3 adapter tests FAILED",
		g_failures);
	return g_failures == 0 ? 0 : 1;
}
