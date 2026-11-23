/*
 * AudioJack.cpp - support for JACK transport
 *
 * Copyright (c) 2005-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "AudioJack.h"

#ifdef MXM_HAVE_JACK

#include <algorithm>
#include <cerrno>

#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QToolButton>
#include <QStringList>
#include <QVector>

#include "AudioBufferView.h"
#include "AudioEngine.h"
#include "ConfigManager.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "MidiJack.h"

#include <cstdio>

namespace mxm {
namespace {

const auto audioJackClass = QString{"audiojack"};
const auto clientNameKey = QString{"clientname"};
const auto disconnectedRepresentation = QString{"-"};

QString getOutputKeyByChannel(size_t channel)
{
	return "output" + QString::number(channel + 1);
}

void printJackStatus(jack_status_t status)
{
	std::fprintf(stderr, "Status: 0x%2.0x\n", static_cast<unsigned int>(status));

	if (status & JackFailure)
	{
		std::fprintf(stderr, "Overall operation failed. JACK dependencies might need to be installed.\n");
	}

	if (status & JackServerFailed)
	{
		std::fprintf(stderr, "Could not connect to JACK server.\n");
	}
}

QString buildChannelSuffix(ch_cnt_t ch)
{
	return (ch % 2 ? "R" : "L") + QString::number(ch / 2 + 1);
}

QString buildOutputName(ch_cnt_t ch)
{
	return QString("master out ") + buildChannelSuffix(ch);
}

auto trackPortName(const QString& trackName, quint64 id, ch_cnt_t channels,
	ch_cnt_t channel) -> QString;

} // namespace

AudioJack::AudioJack(bool& successful, AudioEngine* audioEngineParam)
	: AudioDevice(
		DEFAULT_CHANNELS,
		audioEngineParam)
	, m_client(nullptr)
	, m_active(false)
	, m_midiClient(nullptr)
	, m_tempOutBufs(new jack_default_audio_sample_t*[channels()])
{
	successful = initJackClient();
	if (successful) {
		connect(this, SIGNAL(zombified()), this, SLOT(restartAfterZombified()), Qt::QueuedConnection);
	}

	m_supportsCapture = successful;
}




AudioJack::~AudioJack()
{
	if (m_client != nullptr && m_active)
	{
		jack_deactivate(m_client);
		m_active = false;
	}
	while (!m_trackInputPorts.isEmpty())
	{
		unregisterTrackInputPort(m_trackInputPorts.begin().key());
	}

#ifdef AUDIO_BUS_HANDLE_SUPPORT
	while (m_portMap.size())
	{
		unregisterPort(m_portMap.begin().key());
	}
#endif

	if (m_client != nullptr)
	{
		jack_client_close(m_client);
	}

	delete[] m_tempOutBufs;
}




void AudioJack::restartAfterZombified()
{
	if (initJackClient())
	{
		for (auto it = m_trackInputPorts.begin(); it != m_trackInputPorts.end(); ++it)
		{
			auto& trackInput = it.value();
			trackInput.ports.clear();
			for (ch_cnt_t channel = 0; channel < trackInput.channels; ++channel)
			{
				const auto portName = trackPortName(trackInput.name, it.key(),
					trackInput.channels, channel);
				auto* port = jack_port_register(m_client, portName.toLatin1().constData(),
					JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
				if (port) { trackInput.ports.push_back(port); }
			}
		}
		for (auto it = m_trackInputPorts.cbegin(); it != m_trackInputPorts.cend(); ++it)
		{
			const auto endpoints = it->endpointPorts;
			if (!endpoints.isEmpty()) { setTrackInputSource(it.key(), endpoints); }
		}
		m_active = false;
		m_zombieReported.store(false, std::memory_order_release);
		startProcessing();
		reportDeviceChange(false, QString{}, QStringLiteral("JACK"), true);
		if (m_supportsCapture)
		{
			reportDeviceChange(true, QString{}, QStringLiteral("JACK"), true);
		}
		QMessageBox::information(gui::getGUI()->mainWindow(), tr("JACK client restarted"),
			tr(	"MXM was kicked by JACK for some reason. "
				"Therefore the JACK backend of MXM has been "
				"restarted. You will have to make manual "
				"connections again."));
	}
	else
	{
		reportDeviceChange(false, QStringLiteral("JACK"), QString{}, false);
		if (m_supportsCapture)
		{
			reportDeviceChange(true, QStringLiteral("JACK"), QString{}, false);
		}
		QMessageBox::information(gui::getGUI()->mainWindow(), tr("JACK server down"),
			tr(	"The JACK server seems to have been shutdown "
				"and starting a new instance failed. "
				"Therefore MXM is unable to proceed. "
				"You should save your project and restart "
				"JACK and MXM."));
	}
}




AudioJack* AudioJack::addMidiClient(MidiJack* midiClient)
{
	if (m_client == nullptr) { return nullptr; }

	m_midiClient = midiClient;

	return this;
}


bool AudioJack::initJackClient()
{
	m_outputPorts.clear();

	QString clientName = ConfigManager::inst()->value(audioJackClass, clientNameKey);
	if (clientName.isEmpty()) { clientName = "mxm"; }

	// Will attempt to start the JACK server
	jack_status_t status;
	m_client = jack_client_open(clientName.toLatin1().constData(), JackNullOption, &status);
	if (m_client == nullptr)
	{
		std::fprintf(stderr, "jack_client_open() failed, ");
		printJackStatus(status);

		return false;
	}
	if (status & JackNameNotUnique)
	{
		std::printf("there's already a client with name '%s', so unique "
					"name '%s' was assigned\n",
					clientName.toLatin1().constData(), jack_get_client_name(m_client));
	}

	resizeTrackInputBuffers(jack_get_buffer_size(m_client));

	// set buffer-size callback
	jack_set_buffer_size_callback(m_client,
		[](jack_nframes_t nframes, void* udata) -> int {
			static_cast<AudioJack*>(udata)->resizeTrackInputBuffers(nframes);
			return 0;
		},
		this);

	// set process-callback
	jack_set_process_callback(m_client, staticProcessCallback, this);
	jack_set_port_connect_callback(m_client, portConnectCallback, this);
	jack_set_port_registration_callback(m_client, portRegistrationCallback, this);

	// set shutdown-callback
	jack_on_shutdown(m_client, shutdownCallback, this);

	if (jack_get_sample_rate(m_client) != sampleRate()) { setSampleRate(jack_get_sample_rate(m_client)); }

	for (ch_cnt_t ch = 0; ch < channels(); ++ch)
	{
		const QString name = buildOutputName(ch);
		m_outputPorts.push_back(
			jack_port_register(m_client, name.toLatin1().constData(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0));

		if (m_outputPorts.back() == nullptr)
		{
			std::fprintf(stderr, "no more JACK-ports available!\n");
			return false;
		}
	}

	return true;
}




void AudioJack::resizeTrackInputBuffers(jack_nframes_t nframes)
{
	const auto guard = audioEngine()->requestChangesGuard();
	m_bufferFrames = std::max<jack_nframes_t>(nframes,
		static_cast<jack_nframes_t>(audioEngine()->framesPerAudioBuffer()));

	for (auto& trackInput : m_trackInputPorts)
	{
		trackInput.scratch.assign(
			static_cast<std::size_t>(m_bufferFrames) * trackInput.channels, 0.f);
		trackInput.pointers.resize(trackInput.channels);
		for (ch_cnt_t channel = 0; channel < trackInput.channels; ++channel)
		{
			trackInput.pointers[channel] = trackInput.scratch.data()
				+ static_cast<std::size_t>(channel) * m_bufferFrames;
		}
	}
}

void AudioJack::attemptToConnect(size_t index, const char *mxm_port_type, const char *source_port, const char *destination_port)
{
	std::printf("Attempting to reconnect %s port %u: %s -> %s", mxm_port_type, static_cast<unsigned int>(index), source_port, destination_port);
	if (!jack_connect(m_client, source_port, destination_port))
	{
		std::printf(" - Success!\n");
	}
	else
	{
		std::printf(" - Failure\n");
	}
}

void AudioJack::attemptToReconnectOutput(size_t outputIndex, const QString& targetPort)
{
	if (outputIndex >= m_outputPorts.size()) { return; }

	if (targetPort == disconnectedRepresentation)
	{
		std::fprintf(stderr, "Output port %u is not connected.\n", static_cast<unsigned int>(outputIndex));
		return;
	}

	auto outputName = jack_port_name(m_outputPorts[outputIndex]);
	auto targetName = targetPort.toLatin1();

	attemptToConnect(outputIndex, "output", outputName, targetName.constData());
}

void AudioJack::startProcessingImpl()
{
	if (m_active || m_client == nullptr) { return; }

	if (jack_activate(m_client))
	{
		std::fprintf(stderr, "cannot activate client\n");
		return;
	}

	m_active = true;

	// try to sync JACK's and MXM'ss buffer-size
	//	jack_set_buffer_size( m_client, audioEngine()->framesPerPeriod() );

	const auto cm = ConfigManager::inst();

	const auto numberOfChannels = channels();
	for (size_t i = 0; i < numberOfChannels; ++i)
	{
		attemptToReconnectOutput(i, cm->value(audioJackClass, getOutputKeyByChannel(i)));
	}

}




void AudioJack::stopProcessingImpl()
{
}

void AudioJack::registerPort(AudioBusHandle* port)
{
#ifdef AUDIO_BUS_HANDLE_SUPPORT
	// make sure, port is not already registered
	unregisterPort(port);
	const QString name[2] = {port->name() + " L", port->name() + " R"};

	for (ch_cnt_t ch = 0; ch < DEFAULT_CHANNELS; ++ch)
	{
		m_portMap[port].ports[ch] = jack_port_register(
			m_client, name[ch].toLatin1().constData(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
	}
#else
	(void)port;
#endif
}




void AudioJack::unregisterPort(AudioBusHandle* port)
{
#ifdef AUDIO_BUS_HANDLE_SUPPORT
	if (m_portMap.contains(port))
	{
		for (ch_cnt_t ch = 0; ch < DEFAULT_CHANNELS; ++ch)
		{
			if (m_portMap[port].ports[ch] != nullptr) { jack_port_unregister(m_client, m_portMap[port].ports[ch]); }
		}
		m_portMap.erase(m_portMap.find(port));
	}
#else
	(void)port;
#endif
}

void AudioJack::renamePort(AudioBusHandle* port)
{
#ifdef AUDIO_BUS_HANDLE_SUPPORT
	if (m_portMap.contains(port))
	{
		const QString name[2] = {port->name() + " L", port->name() + " R"};
		for (ch_cnt_t ch = 0; ch < DEFAULT_CHANNELS; ++ch)
		{
#ifdef MXM_HAVE_JACK_PRENAME
			jack_port_rename(m_client, m_portMap[port].ports[ch], name[ch].toLatin1().constData());
#else
			jack_port_set_name(m_portMap[port].ports[ch], name[ch].toLatin1().constData());
#endif
		}
	}
#else
	(void)port;
#endif // AUDIO_BUS_HANDLE_SUPPORT
}




namespace
{

auto trackPortName(const QString& trackName, quint64 id, ch_cnt_t channels, ch_cnt_t channel) -> QString
{
	QString safeName = trackName.simplified();
	safeName.replace(':', '-');
	safeName.replace('\n', ' ');
	if (safeName.isEmpty()) { safeName = QStringLiteral("Track"); }

	QString suffix;
	if (channels == 1) { suffix = QStringLiteral(" In"); }
	else if (channels == 2)
	{
		suffix = channel == 0 ? QStringLiteral(" In L") : QStringLiteral(" In R");
	}
	else { suffix = QStringLiteral(" In %1").arg(channel + 1); }
	return safeName + QStringLiteral(" [%1]").arg(id, 0, 16) + suffix;
}

} // namespace

auto AudioJack::supportsTrackInputPorts() const -> bool
{
	return m_supportsCapture && m_client != nullptr;
}

auto AudioJack::supportsExternalPatchbayInput() const -> bool
{
	return supportsTrackInputPorts();
}

auto AudioJack::inputSources() const -> std::vector<AudioInputSource>
{
	auto sources = std::vector<AudioInputSource>{};
	if (!m_client) { return sources; }

	const char** ports = jack_get_ports(m_client, nullptr, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput);
	if (!ports) { return sources; }

	auto portsByClient = QMap<QString, QStringList>{};
	const auto ownClient = QString::fromUtf8(jack_get_client_name(m_client));
	for (auto index = 0; ports[index] != nullptr; ++index)
	{
		const auto name = QString::fromUtf8(ports[index]);
		const auto separator = name.indexOf(':');
		if (separator <= 0 || name.left(separator) == ownClient) { continue; }
		portsByClient[name.left(separator)].append(name);
	}
	jack_free(ports);

	const auto systemPorts = portsByClient.value(QStringLiteral("system"));
	QStringList defaultPorts;
	for (auto channel = 1; channel <= 8; ++channel)
	{
		const auto port = QStringLiteral("system:capture_%1").arg(channel);
		if (systemPorts.contains(port)) { defaultPorts.append(port); }
	}
	if (!defaultPorts.isEmpty())
	{
		const auto channels = static_cast<ch_cnt_t>(defaultPorts.size());
		sources.push_back(makeAudioInputSource(
			QString::fromLatin1(SystemDefaultInputSourceId), QObject::tr("System Default"),
			channels, defaultPorts));
	}

	for (auto it = portsByClient.cbegin(); it != portsByClient.cend(); ++it)
	{
		const auto& clientPorts = it.value();
		for (qsizetype channel = 0; channel < clientPorts.size(); ++channel)
		{
			const auto endpoint = clientPorts[channel];
			sources.push_back(makeAudioInputSource(
				QStringLiteral("jack-port:") + endpoint,
				QStringLiteral("%1 — %2").arg(it.key(), endpoint.section(':', 1)),
				1, {endpoint}));
			for (auto secondChannel = channel + 1; secondChannel < clientPorts.size(); ++secondChannel)
			{
				const auto nextEndpoint = clientPorts[secondChannel];
				sources.push_back(makeAudioInputSource(
					QStringLiteral("jack-pair:") + endpoint + QLatin1Char('|') + nextEndpoint,
					QStringLiteral("%1 — %2 / %3").arg(it.key(), endpoint.section(':', 1),
						nextEndpoint.section(':', 1)),
					2, {endpoint, nextEndpoint}));
			}
		}
	}

	return sources;
}

void AudioJack::setTrackInputSource(quint64 id, const QStringList& endpointPorts)
{
	if (!m_client) { return; }
	const auto guard = audioEngine()->requestChangesGuard();
	auto found = m_trackInputPorts.find(id);
	if (found == m_trackInputPorts.end()) { return; }

	for (const auto& previous : found->endpointPorts)
	{
		for (auto* port : found->ports)
		{
			if (port) { jack_disconnect(m_client, previous.toUtf8().constData(), jack_port_name(port)); }
		}
	}
	found->endpointPorts.clear();
	if (endpointPorts.isEmpty())
	{
		audioEngine()->setTrackInputCaptureFailed(id, false);
		return;
	}

	const auto channelCount = std::min(static_cast<std::size_t>(endpointPorts.size()), found->ports.size());
	auto failed = endpointPorts.size() > static_cast<qsizetype>(found->ports.size());
	auto newConnections = QVector<QPair<QString, QString>>{};
	for (std::size_t channel = 0; channel < channelCount; ++channel)
	{
		const auto source = endpointPorts[channel].toUtf8();
		if (jack_port_by_name(m_client, source.constData()) == nullptr)
		{
			failed = true;
			break;
		}
		const auto result = jack_connect(m_client, source.constData(), jack_port_name(found->ports[channel]));
		if (result == 0 || result == EEXIST)
		{
			found->endpointPorts.append(endpointPorts[channel]);
			newConnections.append({endpointPorts[channel],
				QString::fromUtf8(jack_port_name(found->ports[channel]))});
		}
		else
		{
			failed = true;
			break;
		}
	}
	if (failed)
	{
		for (const auto& [source, destination] : newConnections)
		{
			jack_disconnect(m_client, source.toUtf8().constData(), destination.toUtf8().constData());
		}
		found->endpointPorts.clear();
	}
	audioEngine()->setTrackInputCaptureFailed(id, failed);
}




auto AudioJack::registerTrackInputPort(quint64 id, const QString& name, ch_cnt_t channels) -> bool
{
	if (!supportsTrackInputPorts() || id == 0 || channels == 0) { return false; }
	const auto guard = audioEngine()->requestChangesGuard();
	unregisterTrackInputPort(id);

	TrackInputPort port;
	port.name = name;
	port.channels = channels;
	for (ch_cnt_t channel = 0; channel < channels; ++channel)
	{
		const auto portName = trackPortName(name, id, channels, channel);
		auto* jackPort = jack_port_register(m_client, portName.toLatin1().constData(),
			JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
		if (jackPort == nullptr)
		{
			for (auto* registered : port.ports)
			{
				jack_port_unregister(m_client, registered);
			}
			return false;
		}
		port.ports.push_back(jackPort);
	}

	const auto bufferFrames = std::max<jack_nframes_t>(m_bufferFrames,
		static_cast<jack_nframes_t>(audioEngine()->framesPerAudioBuffer()));
	port.scratch.assign(static_cast<std::size_t>(bufferFrames) * channels, 0.f);
	port.pointers.resize(channels);
	for (ch_cnt_t channel = 0; channel < channels; ++channel)
	{
		port.pointers[channel] = port.scratch.data()
			+ static_cast<std::size_t>(channel) * bufferFrames;
	}
	m_trackInputPorts.insert(id, std::move(port));
	return true;
}




void AudioJack::unregisterTrackInputPort(quint64 id)
{
	const auto guard = audioEngine()->requestChangesGuard();
	auto found = m_trackInputPorts.find(id);
	if (found == m_trackInputPorts.end()) { return; }
	for (auto* port : found->ports)
	{
		if (m_client != nullptr && port != nullptr) { jack_port_unregister(m_client, port); }
	}
	m_trackInputPorts.erase(found);
}




void AudioJack::renameTrackInputPort(quint64 id, const QString& name)
{
	const auto guard = audioEngine()->requestChangesGuard();
	auto found = m_trackInputPorts.find(id);
	if (found == m_trackInputPorts.end()) { return; }
	for (ch_cnt_t channel = 0; channel < found->channels; ++channel)
	{
		auto* port = found->ports[channel];
		if (port == nullptr) { continue; }
		const auto portName = trackPortName(name, id, found->channels, channel);
#ifdef MXM_HAVE_JACK_PRENAME
		jack_port_rename(m_client, port, portName.toLatin1().constData());
#else
		jack_port_set_name(port, portName.toLatin1().constData());
#endif
	}
}




int AudioJack::processCallback(jack_nframes_t nframes)
{
	// Hold model-owned JACK port structures stable through render and capture
	// publication. renderNextBuffer takes this recursive guard as well.
	const auto guard = audioEngine()->requestChangesGuard();

	// do midi processing first so that midi input can
	// add to the following sound processing
	if (m_midiClient && nframes > 0)
	{
		m_midiClient.load()->JackMidiRead(nframes);
		m_midiClient.load()->JackMidiWrite(nframes);
	}

	for (int c = 0; c < channels(); ++c)
	{
		m_tempOutBufs[c] = (jack_default_audio_sample_t*)jack_port_get_buffer(m_outputPorts[c], nframes);
	}

#ifdef AUDIO_BUS_HANDLE_SUPPORT
	const int frames = std::min<int>(nframes, audioEngine()->framesPerAudioBuffer());
	for (JackPortMap::iterator it = m_portMap.begin(); it != m_portMap.end(); ++it)
	{
		for (ch_cnt_t ch = 0; ch < channels(); ++ch)
		{
			if (it.value().ports[ch] == nullptr) { continue; }
			jack_default_audio_sample_t* buf
				= (jack_default_audio_sample_t*)jack_port_get_buffer(it.value().ports[ch], nframes);
			for (int frame = 0; frame < frames; ++frame)
			{
				buf[frame] = it.key()->buffer()[frame][ch];
			}
		}
	}
#endif

	if (!isRunning())
	{
		for (int c = 0; c < channels(); ++c)
		{
			std::fill_n(m_tempOutBufs[c], nframes, 0.f);
		}
	}
	else
	{
		audioEngine()->renderNextBuffer({m_tempOutBufs, channels(), nframes});
	}

	for (auto it = m_trackInputPorts.begin(); it != m_trackInputPorts.end(); ++it)
	{
		const auto frames = std::min<jack_nframes_t>(nframes, m_bufferFrames);
		for (ch_cnt_t channel = 0; channel < it->channels; ++channel)
		{
			auto* destination = const_cast<float*>(it->pointers[channel]);
			const auto* source = it->ports[channel] != nullptr
				? static_cast<const jack_default_audio_sample_t*>(
					jack_port_get_buffer(it->ports[channel], nframes))
				: nullptr;
			if (source != nullptr) { std::copy_n(source, frames, destination); }
			else { std::fill_n(destination, frames, 0.f); }
		}
		audioEngine()->pushTrackInputFrames(it.key(),
			PlanarBufferView<const float>{it->pointers.data(), it->channels, frames});
	}
	return 0;
}




int AudioJack::staticProcessCallback(jack_nframes_t nframes, void* udata)
{
	return static_cast<AudioJack*>(udata)->processCallback(nframes);
}


void AudioJack::portConnectCallback(jack_port_id_t, jack_port_id_t, int, void* udata)
{
	auto* audioJack = static_cast<AudioJack*>(udata);
	bool anyOutputConnected = false;
	for (auto* port : audioJack->m_outputPorts)
	{
		if (port && jack_port_connected(port) > 0)
		{
			anyOutputConnected = true;
			break;
		}
	}
	if (!audioJack->m_outputPorts.empty())
	{
		if (!anyOutputConnected)
		{
			if (!audioJack->m_portsDisconnectedReported.exchange(true, std::memory_order_acq_rel))
			{
				audioJack->reportDeviceChange(false, QStringLiteral("JACK"), QString{}, false);
			}
		}
		else
		{
			if (audioJack->m_portsDisconnectedReported.exchange(false, std::memory_order_acq_rel))
			{
				audioJack->reportDeviceChange(false, QString{}, QStringLiteral("JACK"), true);
			}
		}
	}
}

void AudioJack::portRegistrationCallback(jack_port_id_t, int, void* udata)
{
	auto* audioJack = static_cast<AudioJack*>(udata);
	QMetaObject::invokeMethod(audioJack, [audioJack] {
		if (auto* engine = audioJack->audioEngine()) { engine->notifyInputSourcesChanged(); }
	}, Qt::QueuedConnection);
}




void AudioJack::shutdownCallback(void* udata)
{
	auto thisClass = static_cast<AudioJack*>(udata);
	thisClass->m_client = nullptr;
	if (!thisClass->m_zombieReported.exchange(true, std::memory_order_acq_rel))
	{
		thisClass->reportDeviceChange(false, QStringLiteral("JACK"), QString{}, false);
		if (thisClass->m_supportsCapture)
		{
			thisClass->reportDeviceChange(true, QStringLiteral("JACK"), QString{}, false);
		}
	}
	emit thisClass->zombified();
}




AudioJack::setupWidget::setupWidget(QWidget* parent)
	: AudioDeviceSetupWidget(AudioJack::name(), parent)
{
	// TODO: Once backend can be changed without restarting MXM, add a button to start/stop
	//       the JACK server and an indicator for the JACK server status.

	jack_status_t status;
	m_client = jack_client_open("MXM-Setup Dialog", JackNoStartServer, &status);
	if (!m_client)
	{
		// Failure is expected when not using the JACK backend
	}

	QFormLayout * form = new QFormLayout(this);

	// Set the field growth policy to allow fields to expand horizontally
	form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

	const auto cm = ConfigManager::inst();
	QString cn = cm->value(audioJackClass, clientNameKey);
	if (cn.isEmpty()) { cn = "mxm"; }
	m_clientName = new QLineEdit(cn, this);

	form->addRow(tr("Client name"), m_clientName);

	auto buildToolButton = [](QWidget* parent, const QString& currentSelection, const std::vector<std::string>& names, const QString& filteredMXMClientName)
	{
		auto toolButton = new QToolButton(parent);
		// Make sure that the tool button will fill out the available space in the form layout
		toolButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
		toolButton->setPopupMode(QToolButton::InstantPopup);
		toolButton->setText(currentSelection);
		auto menu = AudioJack::setupWidget::buildMenu(toolButton, names, filteredMXMClientName);
		toolButton->setMenu(menu);

		return toolButton;
	};

	// Outputs
	const auto audioOutputNames = getAudioOutputNames();

	constexpr size_t numberOfOutputChannels = 2;
	for (size_t i = 0; i < numberOfOutputChannels; ++i)
	{
		const auto outputKey = getOutputKeyByChannel(i);
		const auto outputValue = cm->value(audioJackClass, outputKey);
		auto outputDevice = buildToolButton(this, outputValue, audioOutputNames, cn);
		form->addRow(buildOutputName(i) + ":", outputDevice);
		m_outputDevices.push_back(outputDevice);
	}

	if (m_client != nullptr)
	{
		jack_deactivate(m_client);
		jack_client_close(m_client);
	}
}


void AudioJack::setupWidget::saveSettings()
{
	ConfigManager::inst()->setValue(audioJackClass, clientNameKey, m_clientName->text());

	for (size_t i = 0; i < m_outputDevices.size(); ++i)
	{
		ConfigManager::inst()->setValue(audioJackClass, getOutputKeyByChannel(i), m_outputDevices[i]->text());	
	}

}

std::vector<std::string> AudioJack::setupWidget::getAudioPortNames(JackPortFlags portFlags) const
{
	std::vector<std::string> audioPorts;

	// We are using weak_libjack. If JACK is not installed this will result in the client being nullptr.
	// Because jack_get_ports in weak_libjack does not check for nullptr we have to do this here and fail gracefully,
	// i.e. with an empty list of audio ports.
	if (!m_client)
	{
		return audioPorts;
	}

	const char **inputAudioPorts = jack_get_ports(m_client, nullptr, JACK_DEFAULT_AUDIO_TYPE, portFlags);
	if (inputAudioPorts)
	{
		for (int i = 0; inputAudioPorts[i] != nullptr; ++i)
		{
			auto currentPortName = inputAudioPorts[i];

			audioPorts.push_back(currentPortName);
		}
		jack_free(inputAudioPorts); // Remember to free after use
	}

	return audioPorts;
}

std::vector<std::string> AudioJack::setupWidget::getAudioOutputNames() const
{
	return getAudioPortNames(JackPortIsInput);
}

QMenu* AudioJack::setupWidget::buildMenu(QToolButton* toolButton, const std::vector<std::string>& names, const QString& filteredMXMClientName)
{
	auto menu = new QMenu(toolButton);
	QMap<QString, QMenu*> clientNameToSubMenuMap;
	QList<QAction*> topLevelActions;
	for (const auto& currentName : names)
	{
		const auto clientNameWithPortName = QString::fromStdString(currentName);

		auto actionLambda = [toolButton, clientNameWithPortName](bool checked)
		{
			toolButton->setText(clientNameWithPortName);
		};

		// Split into individual client name and port name
		const auto list = clientNameWithPortName.split(":");
		if (list.size() == 2)
		{
			const auto& clientName = list[0];
			const auto& portName = list[1];

			if (clientName == filteredMXMClientName)
			{
				// Prevent loops by not adding port of the MXM client to the menu
				continue;
			}

			QMenu* clientSubMenu = nullptr;

			auto it = clientNameToSubMenuMap.find(clientName);
			if (it == clientNameToSubMenuMap.end())
			{
				clientSubMenu = new QMenu(menu);
				clientSubMenu->setTitle(clientName);
				clientNameToSubMenuMap.insert(clientName, clientSubMenu);
			}
			else
			{
				clientSubMenu = *it;
			}

			auto action = new QAction(portName, clientSubMenu);
			connect(action, &QAction::triggered, actionLambda);
			clientSubMenu->addAction(action);
		}
		else
		{
			// We cannot split into client and port name. Add the whole thing to the top level menu
			auto action = new QAction(QString::fromStdString(currentName), menu);
			connect(action, &QAction::triggered, actionLambda);
			topLevelActions.append(action);
		}
	}

	// First add the sub menus. By iterating the map they will be sorted automatically
	for (auto it = clientNameToSubMenuMap.begin(); it != clientNameToSubMenuMap.end(); ++it)
	{
		menu->addMenu(it.value());
	}

	// Now add potential top level actions, i.e. the entries which cannot be split at exactly one ":"
	// They must be sorted explicitly
	std::sort(topLevelActions.begin(), topLevelActions.end(), [](QAction* a, QAction* b) { return a->text() < b->text(); });
	menu->addActions(topLevelActions);

	// Add the menu entry which represents the disconnected state at the very end
	auto disconnectedAction = new QAction(disconnectedRepresentation, menu);
	connect(disconnectedAction, &QAction::triggered, [toolButton](bool checked)	{ toolButton->setText(disconnectedRepresentation); });
	menu->addAction(disconnectedAction);

	return menu;
}


} // namespace mxm

#endif // MXM_HAVE_JACK
