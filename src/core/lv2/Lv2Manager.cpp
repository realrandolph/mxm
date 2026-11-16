/*
 * Lv2Manager.cpp - Implementation of Lv2Manager class
 *
 * Copyright (c) 2018-2024 Johannes Lorenz <jlsf2013$users.sourceforge.net, $=@>
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

#include "Lv2Manager.h"

#ifdef MXM_HAVE_LV2

#ifdef MXM_HAVE_LV2_UI
#include <QGuiApplication>
#endif

#include <algorithm>
#include <cstdlib>
#include <lilv/lilv.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/options/options.h>
#include <lv2/worker/worker.h>
#include <QDebug>
#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include "AudioEngine.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "Plugin.h"
#include "Lv2ControlBase.h"
#include "Lv2Options.h"
#include "PluginIssue.h"


namespace mxm
{

struct Lv2Manager::PluginIndexState
{
	std::vector<const LilvPlugin*> plugins;
	std::size_t nextPlugin = 0;
	std::size_t pluginsLoaded = 0;
	std::size_t cacheHits = 0;
	unsigned blocked = 0;
	QElapsedTimer timer;
	QString cachePath;
	QString hostFingerprint;
	QJsonObject cachedPlugins;
	QJsonObject updatedPlugins;
	std::map<QString, QString> bundleFingerprints;
};

namespace
{

constexpr int PluginIndexSchema = 1;

QString bundleMetadataFingerprint(const LilvPlugin* plugin)
{
	const LilvNode* bundleUri = lilv_plugin_get_bundle_uri(plugin);
	char* path = lilv_file_uri_parse(lilv_node_as_uri(bundleUri), nullptr);
	if (!path) { return {}; }

	const QString bundlePath = QString::fromLocal8Bit(path);
	lilv_free(path);
	QDir bundleDir(bundlePath);
	if (!bundleDir.exists()) { return {}; }

	QStringList metadataFiles;
	QDirIterator it(bundlePath, {"*.ttl"}, QDir::Files, QDirIterator::Subdirectories);
	while (it.hasNext()) { metadataFiles.push_back(it.next()); }
	std::sort(metadataFiles.begin(), metadataFiles.end());
	if (metadataFiles.isEmpty()) { return {}; }

	QCryptographicHash hash(QCryptographicHash::Sha256);
	for (const QString& filePath : metadataFiles)
	{
		QFile file(filePath);
		if (!file.open(QIODevice::ReadOnly)) { return {}; }
		hash.addData(QDir(bundlePath).relativeFilePath(filePath).toUtf8());
		hash.addData(file.readAll());
	}
	return QString::fromLatin1(hash.result().toHex());
}

} // namespace


const std::set<std::string_view> Lv2Manager::unstablePlugins =
{
	// github.com/calf-studio-gear/calf, #278
	"http://calf.sourceforge.net/plugins/Analyzer",
	"http://calf.sourceforge.net/plugins/BassEnhancer",
	"http://calf.sourceforge.net/plugins/CompensationDelay",
	"http://calf.sourceforge.net/plugins/Crusher",
	"http://calf.sourceforge.net/plugins/Exciter",
	"http://calf.sourceforge.net/plugins/Saturator",
	"http://calf.sourceforge.net/plugins/StereoTools",
	"http://calf.sourceforge.net/plugins/TapeSimulator",
	"http://calf.sourceforge.net/plugins/TransientDesigner",
	"http://calf.sourceforge.net/plugins/Vinyl",

	// https://gitlab.com/drobilla/blop-lv2/-/issues/3
	"http://drobilla.net/plugins/blop/pulse",
	"http://drobilla.net/plugins/blop/sawtooth",
	"http://drobilla.net/plugins/blop/square",
	"http://drobilla.net/plugins/blop/triangle",

	// unstable
	"urn:juced:DrumSynth"
};

const std::set<std::string_view> Lv2Manager::pluginsOnlyUsefulWithUi =
{
	// Visualization, meters, and scopes etc., won't work if UI is disabled
	"http://distrho.sf.net/plugins/ProM",
	"http://distrho.sf.net/plugins/glBars",
	"http://gareus.org/oss/lv2/meters#spectr30mono",
	"http://gareus.org/oss/lv2/meters#spectr30stereo",
	"http://gareus.org/oss/lv2/meters#bitmeter",
	"http://gareus.org/oss/lv2/meters#BBCM6",
	"http://gareus.org/oss/lv2/meters#BBCmono",
	"http://gareus.org/oss/lv2/meters#BBCstereo",
	"http://gareus.org/oss/lv2/meters#DINmono",
	"http://gareus.org/oss/lv2/meters#DINstereo",
	"http://gareus.org/oss/lv2/meters#EBUmono",
	"http://gareus.org/oss/lv2/meters#EBUstereo",
	"http://gareus.org/oss/lv2/meters#EBUr128",
	"http://gareus.org/oss/lv2/meters#BBCM6",
	"http://gareus.org/oss/lv2/meters#dr14mono",
	"http://gareus.org/oss/lv2/meters#dr14stereo",
	"http://gareus.org/oss/lv2/meters#K12mono",
	"http://gareus.org/oss/lv2/meters#K12stereo",
	"http://gareus.org/oss/lv2/meters#K14mono",
	"http://gareus.org/oss/lv2/meters#K14stereo",
	"http://gareus.org/oss/lv2/meters#K20mono",
	"http://gareus.org/oss/lv2/meters#K20stereo",
	"http://gareus.org/oss/lv2/meters#NORmono",
	"http://gareus.org/oss/lv2/meters#NORstereo",
	"http://gareus.org/oss/lv2/meters#COR",
	"http://gareus.org/oss/lv2/meters#dBTPmono",
	"http://gareus.org/oss/lv2/meters#dBTPstereo",
	"http://gareus.org/oss/lv2/meters#TPnRMSmono",
	"http://gareus.org/oss/lv2/meters#TPnRMSstereo",
	"http://gareus.org/oss/lv2/meters#VUmono",
	"http://gareus.org/oss/lv2/meters#VUstereo",
	"http://gareus.org/oss/lv2/meters#goniometer",
	"http://gareus.org/oss/lv2/meters#stereoscope",
	"http://gareus.org/oss/lv2/meters#SigDistHist",
	"http://gareus.org/oss/lv2/tuna#one",
	"http://gareus.org/oss/lv2/tuna#two",
	"http://gareus.org/oss/lv2/sisco#Mono",
	"http://gareus.org/oss/lv2/sisco#Stereo",
	"http://gareus.org/oss/lv2/spectra#Mono",
	"http://gareus.org/oss/lv2/convoLV2#Mono",
	"http://gareus.org/oss/lv2/convoLV2#MonoToStereo",
	"http://gareus.org/oss/lv2/convoLV2#Stereo",
	"http://gareus.org/oss/lv2/zeroconvolv#CfgMono",
	"http://gareus.org/oss/lv2/zeroconvolv#CfgMonoToStereo",
	"http://gareus.org/oss/lv2/zeroconvolv#CfgStereo",
	"http://gareus.org/oss/lv2/zeroconvolv#Mono",
	"http://gareus.org/oss/lv2/zeroconvolv#MonoToStereo",
	"http://gareus.org/oss/lv2/zeroconvolv#Stereo",
	"http://lsp-plug.in/plugins/lv2/latency_meter",
	"http://lsp-plug.in/plugins/lv2/spectrum_analyzer_x1",
	"http://lsp-plug.in/plugins/lv2/spectrum_analyzer_x2",
	"http://lsp-plug.in/plugins/lv2/phase_detector",
	"http://lsp-plug.in/plugins/lv2/profiler_mono",
	"http://lsp-plug.in/plugins/lv2/profiler_stereo",
	"http://invadarecords.com/plugins/lv2/meter",
	"http://guitarix.sourceforge.net/plugins/gxtuner#tuner",
	"https://github.com/jpcima/ADLplug",
	"https://github.com/HiFi-LoFi/KlangFalter",
	"https://github.com/klangfreund/SpectrumAnalyser",
	"https://github.com/klangfreund/lufsmeter",
	"https://github.com/laixinyuan/StereoSourceSepartion",
	"urn:juce:TalFilter2",
	"urn:juce:Vex",
	"http://zynaddsubfx.sourceforge.net",
	"http://geontime.com/geonkick/single"
};

const std::set<std::string_view> Lv2Manager::unstablePluginsBuffersizeLessEqual32 =
{
	"http://moddevices.com/plugins/mod-devel/2Voices",
	"http://moddevices.com/plugins/mod-devel/Capo",
	"http://moddevices.com/plugins/mod-devel/Drop",
	"http://moddevices.com/plugins/mod-devel/Harmonizer",
	"http://moddevices.com/plugins/mod-devel/Harmonizer2",
	"http://moddevices.com/plugins/mod-devel/HarmonizerCS",
	"http://moddevices.com/plugins/mod-devel/SuperCapo",
	"http://moddevices.com/plugins/mod-devel/SuperWhammy",
	"http://moddevices.com/plugins/mod-devel/Gx2Voices",
	"http://moddevices.com/plugins/mod-devel/GxCapo",
	"http://moddevices.com/plugins/mod-devel/GxDrop",
	"http://moddevices.com/plugins/mod-devel/GxHarmonizer",
	"http://moddevices.com/plugins/mod-devel/GxHarmonizer2",
	"http://moddevices.com/plugins/mod-devel/GxHarmonizerCS",
	"http://moddevices.com/plugins/mod-devel/GxSuperCapo",
	"http://moddevices.com/plugins/mod-devel/GxSuperWhammy"
};




Lv2Manager::Lv2Manager() :
	m_wantUi(wantUi()),
	m_uridCache(m_uridMap)
{
	const char* dbgStr = getenv("MXM_LV2_DEBUG");
	m_debug = (dbgStr && *dbgStr);

	m_world = lilv_world_new();
	lilv_world_load_all(m_world);

	m_supportedFeatureURIs.insert(LV2_URID__map);
	m_supportedFeatureURIs.insert(LV2_URID__unmap);
	m_supportedFeatureURIs.insert(LV2_OPTIONS__options);
	m_supportedFeatureURIs.insert(LV2_WORKER__schedule);
	// min/max is always passed in the options
	m_supportedFeatureURIs.insert(LV2_BUF_SIZE__boundedBlockLength);
	// block length is only changed initially in AudioEngine CTOR
	m_supportedFeatureURIs.insert(LV2_BUF_SIZE__fixedBlockLength);
	if (const auto fpp = Engine::audioEngine()->framesPerPeriod(); (fpp & (fpp - 1)) == 0)  // <=> ffp is power of 2 (for ffp > 0)
	{
		m_supportedFeatureURIs.insert(LV2_BUF_SIZE__powerOf2BlockLength);
	}

	auto supportOpt = [this](Lv2UridCache::Id id)
	{
		Lv2Options::supportOption(uridCache()[id]);
	};
	supportOpt(Lv2UridCache::Id::param_sampleRate);
	supportOpt(Lv2UridCache::Id::bufsz_maxBlockLength);
	supportOpt(Lv2UridCache::Id::bufsz_minBlockLength);
	supportOpt(Lv2UridCache::Id::bufsz_nominalBlockLength);
	supportOpt(Lv2UridCache::Id::bufsz_sequenceSize);
}




Lv2Manager::~Lv2Manager()
{
	lilv_world_free(m_world);
}




AutoLilvNode Lv2Manager::uri(const char *uriStr)
{
	return AutoLilvNode(lilv_new_uri(m_world, uriStr));
}




const LilvPlugin *Lv2Manager::getPlugin(const std::string &uri)
{
	auto itr = m_lv2InfoMap.find(uri);
	if (itr != m_lv2InfoMap.end()) { return itr->second.plugin(); }
	if (m_pluginsReady) { return nullptr; }
	AutoLilvNode pluginUri = this->uri(uri.c_str());
	return lilv_plugins_get_by_uri(lilv_world_get_all_plugins(m_world), pluginUri.get());
}




const LilvPlugin *Lv2Manager::getPlugin(const QString &uri)
{
	return getPlugin(uri.toStdString());
}




void Lv2Manager::initPlugins()
{
	if (m_pluginsReady) { return; }
	beginPluginIndexing();
	while (!indexPluginBatch(64)) {}
}


void Lv2Manager::startPluginIndexing()
{
	if (!m_pluginsReady) { beginPluginIndexing(); }
}


void Lv2Manager::beginPluginIndexing()
{
	if (m_pluginsReady || m_pluginIndexState) { return; }
	m_lv2InfoMap.clear();
	m_pluginIndexState = std::make_unique<PluginIndexState>();
	PluginIndexState& state = *m_pluginIndexState;
	state.timer.start();
	const LilvPlugins* plugins = lilv_world_get_all_plugins(m_world);
	LILV_FOREACH(plugins, itr, plugins)
	{
		state.plugins.push_back(lilv_plugins_get(plugins, itr));
	}
	const QString cacheDirectory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
	state.cachePath = cacheDirectory.isEmpty() ? QString() : cacheDirectory + "/lv2-plugin-index.json";
	state.hostFingerprint = QString("schema=%1;ui=%2;period=%3;blocked=%4")
		.arg(PluginIndexSchema)
		.arg(m_wantUi)
		.arg(Engine::audioEngine()->framesPerPeriod())
		.arg(ConfigManager::enableBlockedPlugins());
	for (const auto& feature : m_supportedFeatureURIs)
	{
		state.hostFingerprint += ";" + QString::fromUtf8(feature.data(), feature.size());
	}
	if (!m_debug && !state.cachePath.isEmpty())
	{
		QFile cacheFile(state.cachePath);
		if (cacheFile.open(QIODevice::ReadOnly))
		{
			const QJsonObject root = QJsonDocument::fromJson(cacheFile.readAll()).object();
			if (root.value("host").toString() == state.hostFingerprint)
			{
				state.cachedPlugins = root.value("plugins").toObject();
			}
		}
	}
}


bool Lv2Manager::indexPluginBatch(std::size_t maxPlugins)
{
	if (m_pluginsReady) { return true; }
	if (!m_pluginIndexState) { beginPluginIndexing(); }
	PluginIndexState& state = *m_pluginIndexState;
	const std::size_t end = std::min(state.nextPlugin + maxPlugins, state.plugins.size());
	for (; state.nextPlugin < end; ++state.nextPlugin)
	{
		const LilvPlugin* curPlug = state.plugins[state.nextPlugin];
		const std::string pluginUri = lilv_node_as_uri(lilv_plugin_get_uri(curPlug));
		const LilvNode* bundleUri = lilv_plugin_get_bundle_uri(curPlug);
		const QString bundle = QString::fromUtf8(lilv_node_as_uri(bundleUri));
		auto fingerprintIt = state.bundleFingerprints.find(bundle);
		if (fingerprintIt == state.bundleFingerprints.end())
		{
			fingerprintIt = state.bundleFingerprints.emplace(bundle,
				bundleMetadataFingerprint(curPlug)).first;
		}
		const QString fingerprint = fingerprintIt->second;
		const QJsonObject cached = state.cachedPlugins.value(QString::fromStdString(pluginUri)).toObject();
		const int cachedType = cached.value("type").toInt(-1);
		const bool cachedTypeIsValid = cachedType == static_cast<int>(Plugin::Type::Instrument) ||
			cachedType == static_cast<int>(Plugin::Type::Effect) ||
			cachedType == static_cast<int>(Plugin::Type::Undefined);
		const bool cacheHit = !fingerprint.isEmpty() &&
			cached.value("bundle").toString() == fingerprint &&
			cachedTypeIsValid && cached.value("valid").isBool() &&
			cached.value("blocked").isBool();
		if (cacheHit) { ++state.cacheHits; }

		Plugin::Type type;
		bool valid;
		bool isBlocked;
		if (cacheHit)
		{
			type = static_cast<Plugin::Type>(cachedType);
			valid = cached.value("valid").toBool();
			isBlocked = cached.value("blocked").toBool();
		}
		else
		{
			std::vector<PluginIssue> issues;
			type = Lv2ControlBase::check(curPlug, issues);
			std::sort(issues.begin(), issues.end());
			auto last = std::unique(issues.begin(), issues.end());
			issues.erase(last, issues.end());
			valid = issues.empty();
			isBlocked = std::any_of(issues.begin(), issues.end(),
				[](const PluginIssue& issue) { return issue.type() == PluginIssueType::Blocked; });
			if (m_debug && !issues.empty())
			{
				qDebug() << "Lv2 plugin" << qStringFromPluginNode(curPlug, lilv_plugin_get_name)
					<< "(URI:" << pluginUri.c_str() << ") can not be loaded:";
				for (const PluginIssue& issue : issues) { qDebug() << "  - " << issue; }
			}
		}
		if (!fingerprint.isEmpty())
		{
			QJsonObject record;
			record.insert("bundle", fingerprint);
			record.insert("type", static_cast<int>(type));
			record.insert("valid", valid);
			record.insert("blocked", isBlocked);
			state.updatedPlugins.insert(QString::fromStdString(pluginUri), record);
		}

		Lv2Info info(curPlug, type, valid);

		m_lv2InfoMap[pluginUri] = std::move(info);
		if (valid) { ++state.pluginsLoaded; }
		if (isBlocked) { ++state.blocked; }
	}
	if (state.nextPlugin < state.plugins.size()) { return false; }

	if (!m_debug && !state.cachePath.isEmpty())
	{
		QDir().mkpath(QFileInfo(state.cachePath).absolutePath());
		QSaveFile cacheFile(state.cachePath);
		if (cacheFile.open(QIODevice::WriteOnly))
		{
			QJsonObject root;
			root.insert("host", state.hostFingerprint);
			root.insert("plugins", state.updatedPlugins);
			cacheFile.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
			cacheFile.commit();
		}
	}

	qDebug() << "Lv2 plugin SUMMARY:"
		<< state.pluginsLoaded << "of" << state.plugins.size() << " loaded in"
		<< state.timer.elapsed() << "msecs (" << state.cacheHits << " cached).";
	if(state.pluginsLoaded != state.plugins.size())
	{
		if (m_debug)
		{
			qDebug() <<
				"If you don't want to see all this debug output, please set\n"
				"  environment variable \"MXM_LV2_DEBUG\" to empty or\n"
				"  do not set it.";
		}
		else
		{
			qDebug() <<
				"For details about not loaded plugins, please set\n"
				"  environment variable \"MXM_LV2_DEBUG\" to nonempty.";
		}
	}

	// TODO: might be better in the MXM core
	if(ConfigManager::enableBlockedPlugins())
	{
		qWarning() <<
			"WARNING! Blocked plugins enabled! If you want to disable them,\n"
			"  please set environment variable \"MXM_ENABLE_BLOCKED_PLUGINS\" to empty or\n"
			"  do not set it.";
	}
	else if(state.blocked > 0)
	{
		qDebug() <<
			"Blocked Lv2 Plugins:" << state.blocked << "of" << state.plugins.size() << "\n"
			"  If you want to enable them (dangerous!), please set\n"
			"  environment variable \"MXM_ENABLE_BLOCKED_PLUGINS\" to nonempty.";
	}
	finishPluginIndexing();
	return true;
}


void Lv2Manager::finishPluginIndexing()
{
	m_pluginsReady = true;
	m_pluginIndexState.reset();
}




bool Lv2Manager::isFeatureSupported(const char *featName) const
{
	return m_supportedFeatureURIs.find(featName) != m_supportedFeatureURIs.end();
}




AutoLilvNodes Lv2Manager::findNodes(const LilvNode *subject,
	const LilvNode *predicate, const LilvNode *object)
{
	return AutoLilvNodes(lilv_world_find_nodes (m_world, subject, predicate, object));
}




bool Lv2Manager::wantUi()
{
#ifdef MXM_HAVE_LV2_UI
	return QGuiApplication::platformName() == QStringLiteral("xcb");
#else
	return false;
#endif
}




bool Lv2Manager::isSubclassOf(const LilvPluginClass* pluginClass, const char* classUri)
{
	AutoLilvNode targetUri = uri(classUri);
	const LilvPluginClasses* classes = lilv_world_get_plugin_classes(m_world);
	while (pluginClass)
	{
		if (lilv_node_equals(lilv_plugin_class_get_uri(pluginClass), targetUri.get()))
		{
			return true;
		}

		const LilvNode* parentUri = lilv_plugin_class_get_parent_uri(pluginClass);
		pluginClass = parentUri
			? lilv_plugin_classes_get_by_uri(classes, parentUri)
			: nullptr;
	}
	return false;
}


} // namespace mxm

#endif // MXM_HAVE_LV2
