# Audio channel layouts and capture

MXM stores and records audio with a runtime channel count. A take is no longer
silently reduced to stereo when the capture device provides more channels.

## Metadata

`mxm::SampleBuffer` keeps frame count, sample rate, channel count, channel order
and `mxm::AudioChannelLayout` as separate pieces of metadata. Standard layouts
have explicit speaker roles:

| Layout | Channels | Order |
| --- | --- | --- |
| Mono | 1 | FC |
| Stereo | 2 | FL, FR |
| 5.1 | 6 | FL, FR, FC, LFE, BL, BR |
| 7.1 | 8 | FL, FR, FC, LFE, BL, BR, SL, SR |

A device or file without speaker information produces a **discrete** layout.
A six-channel discrete recording is six channels in capture order; it is not
assumed to be 5.1. Project files store `channels` and `channel_layout` next to
the existing embedded sample data, so old stereo projects load unchanged.

## Capture

* PortAudio and SDL capture every channel reported by the opened device.
* JACK registers `audiojack/inputchannels` input ports (default 2, maximum
  `MaxChannelsPerAudioBuffer`). Unconnected ports are silent. With exactly two
  configured channels and one connected port, the legacy mono-to-both-channels
  convenience is retained.
* Backends without capture support report that explicitly through
  `AudioDevice::supportsCapture()`.
* `AudioEngine::pushInputFrames()` accepts planar or interleaved blocks with a
  runtime channel count. Capture data is handed to the render thread through a
  bounded single-producer/single-consumer ring; buffers are preallocated by
  `AudioEngine::setInputChannels()` and never grow on the audio callback.
* Vorbis multichannel imports are reordered from the Vorbis I channel order to
  MXM's canonical WAV/SMPTE-style order before storage, so speaker roles and
  downmixes stay correct.

## Recording and storage

`SampleCapture` copies audio into a bounded, preallocated ring buffer. A writer
thread drains the ring to a temporary WAV file, so a take is not limited by
memory and the audio callback does not allocate or perform file I/O. Queue
overflow, disk errors and the number of recorded frames are exposed through
`SampleCapture`. `SampleTrack::stopRecording()` finalizes the take on the UI
thread and reports overflow or disk failures in the project error list.

## Import and export

File import preserves the channel count and, when the file carries a channel
mask, the speaker roles. The clip context menu offers **Export sample...**,
which writes the stored channels to a WAV file and writes the known speaker
roles into the file's channel mask. Multichannel clips are never exported as
silently truncated stereo.

## Native track, bus and master layouts

Instrument tracks, sample tracks, mixer channels and the master bus each own a
`mxm::AudioChannelLayout`. The layout is independent of the channel count and
carries explicit speaker roles. The UI offers these presets:

| Preset | Channels | Order |
| --- | --- | --- |
| Mono | 1 | FC |
| Stereo | 2 | FL, FR |
| 3.0 | 3 | FL, FR, FC |
| 4.0 | 4 | FL, FR, BL, BR |
| 5.0 | 5 | FL, FR, FC, BL, BR |
| 5.1 | 6 | FL, FR, FC, LFE, BL, BR |
| 6.1 | 7 | FL, FR, FC, LFE, BL, BR, BC |
| 7.1 | 8 | FL, FR, FC, LFE, BL, BR, SL, SR |
| 7.1.2 | 10 | FL, FR, FC, LFE, BL, BR, SL, SR, TFL, TFR |

The engine itself is not limited to these presets. A loaded `discrete:N` or
`roles:...` layout stays selectable in the project and is allocated at its
runtime channel count (up to `MaxChannelsPerAudioBuffer`).

Project files store `channel_layout` on the track element and on every
`mixerchannel` element. Projects without the attribute load as stereo.

## Routing and conversion

Every track output (`AudioBusHandle`) and mixer channel renders into a planar
`AudioBuffer` whose logical channel count is the selected layout. Sends and
receives are converted between layouts by speaker role:

* matching roles map one-to-one, including LFE;
* stereo sources feed the destination front pair;
* mono sources feed FC when the destination has it, otherwise the front pair;
* surround sources fold to stereo with the documented gains below;
* discrete layouts map by channel index, and extra source channels are folded
  into the destination instead of being discarded;
* hardware output layouts are bound to the standard speaker order for their
  channel count.

This conversion is implemented by `addFrameConverted()` and
`addPlanarConverted()`. The mixer uses a fast same-layout path for stereo.

The track pan control acts as a left/right balance over the speaker field:
left-side speaker roles follow the left gain, right-side roles follow the right
gain, and centre/LFE roles are left at track volume.

Stereo-only effects and plugins still receive the front stereo pair. Remaining
surround channels pass through the effect chain untouched, so a stereo plugin
never silently removes surround content. Stereo instrument plugins feed the
front pair of a surround track bus; native multichannel instrument outputs are
part of #7's multi-I/O plugin routing.

## Playback and monitoring

Sample playback preserves the native sample channel layout when the track bus
is multichannel: the sample is rendered at its stored channel count, resampled
per channel, and converted directly into the bus layout. When no multichannel
destination exists, stored surround clips are folded explicitly to stereo:

| Source channel | Stereo mapping |
| --- | --- |
| FL | left |
| FR | right |
| BL, SL | left at -3 dB |
| BR, SR | right at -3 dB |
| FC, FLC, FRC | both at -3 dB |
| LFE | both at -6 dB |
| BC, top channels | both at -3 dB |
| mono | both unchanged |
| discrete channel 0/1 | left/right |
| other discrete channels | alternate left/right at -3 dB |

The mapping is implemented by `downmixFrameToStereo()` and `downmixToStereo()`.
It is applied at clip thumbnails, input monitoring and any boundary that only
accepts stereo; stored recordings keep their original channels.

## Master rendering and export

The master bus mixes in its selected layout. Hardware devices map the master
layout to their configured output channel count, so multichannel interfaces
receive channels in the standard order for that count. JACK track buses expose
one port per logical channel (named by speaker role) instead of a fixed L/R
pair.

WAV and FLAC export use the master layout channel count and write the speaker
mask for multichannel WAV through libsndfile. Encoders that only support stereo
(MP3, and OGG in this version) receive an explicit surround-to-stereo downmix
rather than truncated or zero-filled channels.
