/*
 * Vst3RenderTest.cpp - headless end-to-end render test for native VST3
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

#include <cstdio>

#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>

#include "Vst3Manager.h"

namespace
{

//! Render a one-note project through the first available VST3 instrument.
int render(const QString& mxmBinary)
{
	using namespace mxm;

	Vst3Manager& manager = Vst3Manager::instance();
	manager.discover();

	const Vst3Manager::Descriptor* instrument = nullptr;
	for (const auto& desc : manager.descriptors())
	{
		if (desc.isInstrument)
		{
			instrument = &desc;
			break;
		}
	}

	if (!instrument)
	{
		std::printf("No VST3 instrument found; skipping render check.\n");
		return 0;
	}

	std::printf("Rendering VST3 instrument: %s (%s)\n",
		qPrintable(instrument->name), qPrintable(QString::fromStdString(instrument->cid)));

	QString mmp = QStringLiteral(
		R"(<?xml version="1.0"?>
<!DOCTYPE multimedia-project>
<multimedia-project version="1.0" creator="MXM" creatorversion="1.3.0" type="song" >
  <head timesig_numerator="4" mastervol="100" timesig_denominator="4" bpm="120" masterpitch="0" />
  <song>
    <trackcontainer width="600" x="5" y="5" maximized="0" height="300" visible="1" type="song" minimized="0" >
      <track muted="0" type="0" name="VST3" >
        <instrumenttrack pan="0" mixch="0" pitch="0" basenote="57" vol="100" >
          <instrument name="vst3instrument" >
            <vst3instrument>
              <key>
                <attribute name="module" value="%1" />
                <attribute name="cid" value="%2" />
              </key>
            </vst3instrument>
          </instrument>
          <midiport inputcontroller="0" fixedoutputvelocity="-1" inputchannel="0" outputcontroller="0" writable="0" outputchannel="1" fixedinputvelocity="-1" outputprogram="1" readable="0" />
          <fxchain numofeffects="0" enabled="0" />
        </instrumenttrack>
        <pattern steps="16" muted="0" type="0" name="VST3" pos="0" len="192" frozen="0" >
          <note len="192" key="60" vol="100" pos="0" pan="0" />
        </pattern>
      </track>
      <track muted="0" type="5" name="Automation track" >
        <automationtrack/>
      </track>
    </trackcontainer>
    <track muted="0" type="6" name="Automation track" >
      <automationtrack/>
      <automationpattern name="Tempo" pos="0" >
        <time value="120" pos="0" />
        <object id="7840741" />
      </automationpattern>
      <automationpattern name="Master-Lautstärke" pos="0" >
        <time value="100" pos="0" />
        <object id="2331772" />
      </automationpattern>
    </track>
    <mixer width="865" x="5" y="310" maximized="0" height="278" visible="1" minimized="0" >
      <mixerchannel num="0" muted="0" volume="1" name="Master" >
        <fxchain numofeffects="0" enabled="0" />
      </mixerchannel>
      <mixerchannel num="1" muted="0" volume="1" name="Channel 1" >
        <fxchain numofeffects="0" enabled="0" />
      </mixerchannel>
    </mixer>
    <controllerrackview width="258" x="880" y="310" maximized="0" height="278" visible="1" minimized="0" />
    <pianoroll width="840" x="-11" y="0" maximized="0" height="480" visible="0" minimized="0" />
    <automationeditor width="740" x="0" y="0" maximized="0" height="480" visible="0" minimized="0" />
    <timeline lp1pos="192" lp0pos="0" lpstate="0" />
    <controllers/>
  </song>
</multimedia-project>
)")
		.arg(QString::fromStdString(instrument->modulePath), QString::fromStdString(instrument->cid));

	QTemporaryDir tmp;
	if (!tmp.isValid())
	{
		std::fprintf(stderr, "FAIL: could not create temporary directory\n");
		return 1;
	}

	const QString mmpPath = tmp.filePath(QStringLiteral("vst3_test.mmp"));
	const QString wavPath = tmp.filePath(QStringLiteral("vst3_test.wav"));

	QFile mmpFile(mmpPath);
	if (!mmpFile.open(QIODevice::WriteOnly))
	{
		std::fprintf(stderr, "FAIL: could not write project file\n");
		return 1;
	}
	mmpFile.write(mmp.toUtf8());
	mmpFile.close();

	QProcess process;
	process.start(mxmBinary, {QStringLiteral("render"), mmpPath, QStringLiteral("--output"), wavPath});
	if (!process.waitForFinished(300000))
	{
		std::fprintf(stderr, "FAIL: mxm render timed out\n");
		return 1;
	}
	if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
	{
		std::fprintf(stderr, "FAIL: mxm render failed:\n%s\n",
			qPrintable(QString::fromLocal8Bit(process.readAllStandardError())));
		return 1;
	}

	// Check the WAV is not silent by looking for any non-zero sample in the
	// data chunk.
	QFile wavFile(wavPath);
	if (!wavFile.open(QIODevice::ReadOnly))
	{
		std::fprintf(stderr, "FAIL: render did not produce a WAV file\n");
		return 1;
	}

	const QByteArray wav = wavFile.readAll();
	// Locate the "data" chunk.
	const int dataIndex = wav.indexOf("data");
	if (dataIndex < 0)
	{
		std::fprintf(stderr, "FAIL: WAV file has no data chunk\n");
		return 1;
	}
	const QByteArray samples = wav.mid(dataIndex + 8);
	bool nonSilent = false;
	for (const char c : samples)
	{
		if (c != '\0')
		{
			nonSilent = true;
			break;
		}
	}

	if (!nonSilent)
	{
		std::fprintf(stderr, "FAIL: rendered WAV is silent; the VST3 instrument produced no audio\n");
		return 1;
	}

	std::printf("VST3 render check PASSED (%s)\n", qPrintable(instrument->name));
	return 0;
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	std::setvbuf(stdout, nullptr, _IONBF, 0);

	if (argc < 2)
	{
		std::fprintf(stderr, "usage: Vst3RenderTest <mxm-binary>\n");
		return 2;
	}

	return render(QString::fromLocal8Bit(argv[1]));
}
