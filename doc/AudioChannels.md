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

* SDL opens the selected input device per track and captures its first two
  channels (duplicating mono to stereo). PortAudio captures every channel
  reported by its configured device.
* JACK exposes audio inputs through the per-track source assignment and
  registers input ports for each recordable track. Its capture channel count is
  not configured globally.
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

## Playback and monitoring

The existing mixer, instrument and plugin paths are stereo. Multichannel clips
are preserved in storage and mapped explicitly when they reach a stereo
boundary:

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
It is applied at playback, clip thumbnails and input monitoring only; stored
recordings keep their original channels. Native surround mixing and surround
master export are separate work tracked by #63.
