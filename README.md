<div align="center">
	<p>
		<a href="https://github.com/realrandolph/mxm">
			<img src="branding/README-banner.png" alt="MXM — Musica ex Machina" width="640">
		</a>
	</p>
	<p><b>Musica ex Machina</b> — pronounced <i>"Mix 'em"</i></p>
	<p>Cross-platform music production software</p>
	<p>
		<a href="https://github.com/realrandolph/mxm">Repository</a>
		⦁︎
		<a href="https://github.com/realrandolph/mxm/releases">Releases</a>
	</p>
</div>

What is MXM?
--------------

MXM (Musica ex Machina) is an independent, open-source, cross-platform digital
audio workstation designed for music production. It descends from
[LMMS](https://lmms.io/) and retains full compatibility with existing `.mmp`
and `.mmpz` project files.
Older project formats remain supported; required migrations run when a project is
opened, and saving writes the current format.

MXM includes an advanced Piano Roll, Beat Sequencer, Song Editor, and Mixer for
composing, arranging, recording, and mixing music. It ships with 15+ synthesizer
plugins and supports VST2 and VST3 instruments and effects, LV2 plugins, and
SoundFont2 sound banks.

Features
---------

* **Record performances directly into your arrangement.** Capture MIDI on an
  instrument track, starting immediately, after a three-second delay, or
  when you play the first note. Choose a recording channel or preserve incoming
  MIDI channels; the take grows with the playhead and keeps its recorded length.
* **Record audio on Sample Tracks.** Start immediately, after a three-second
  delay, or when input is detected, with input monitoring and a level meter.
* **Keep overlapping clips readable.** Channel-colored MIDI notes and sample
  waveforms remain visible above other clips in the Song Editor.
* Compose and arrange melodies, samples, patterns, and automation in the Song
  Editor, Pattern Editor, and Piano Roll.
* Mix with unlimited mixer channels and an arbitrary number of effects.
* Resize Instrument Windows around plugin interfaces while preserving their
  intended proportions.
* Get started with the detected audio device; audio settings are available if
  playback setup fails.
* Extend the built-in instrument and effect collection with VST2, VST3, and LV2
  plugins, SoundFont2, GUS Patches, and full MIDI support.
* Import and export MIDI files; create user-defined track automation and use
  computer-controlled automation sources.

Building
---------

See [Compiling](https://github.com/realrandolph/mxm/blob/master/INSTALL.txt).

The build produces the `mxm` executable.

Run the test suite in parallel with
`ctest --test-dir build/tests --output-on-failure -j4`.

Join development
----------------

If you are interested in MXM, its programming, artwork, testing, writing demo
songs, (and improving this README...) or something like that, you're welcome to
participate in its development!

Before coding a new big feature, please _always_ [file an issue](https://github.com/realrandolph/mxm/issues/new)
for your idea and suggestions about your feature and about the intended
implementation, and wait for replies. There may be different ideas,
improvements, or hints, or your feature may not be welcome or needed at the
moment.

Attribution
-----------

MXM is a fork of [LMMS](https://github.com/LMMS/lmms). It is distributed under
the terms of the GNU General Public License (see `LICENSE.txt`). See `AUTHORS`
and `CONTRIBUTORS` for the full list of contributors.
