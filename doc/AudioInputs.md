# Per-track audio inputs

Every recordable Sample Track owns its own capture assignment. The **Input**
dropdown selects a named recording device/source for that track; there is no
global recording-input setting. JACK additionally allows manual patchbay
routing to the track's input ports.

## Assignment model

`mxm::AudioInputSource` describes one selectable endpoint:

- stable `id`;
- human-readable `displayName`;
- source `channels` and `AudioChannelLayout`;
- `endpointPorts` for independently opened or backend-routed per-track sources.

New tracks and projects without an explicit assignment default to the backend's
**System Default** input. The track stores its `input_source` id in the project.

The list is rebuilt from the active backend. A saved assignment that no longer
exists is shown as unavailable; recording does not silently fall back to a
different source.

## Destination policy

Physical input selection is separate from the existing **Record to** control:

- **All channels (Auto)** captures the selected source's own channels/layout;
- **Left channel only** / **Right channel only** route the selected source into
  the left or right destination channel of a stereo take.

`SampleCapture` supports an explicit source-to-destination map so channels can
be reordered or duplicated independently of the selected device.

## Monitoring, metering and triggering

Monitoring, the track input meter and input-triggered recording use the
assigned source channels. They do not read the engine-wide input peak. If an
assignment becomes unavailable during a take, the take is not remapped: the
track records silence and reports the problem when recording stops.

## Backend behavior

SDL and PortAudio enumerate capture devices and open the selected device for
each assigned track. ALSA enumerates capture-capable PCM devices and captures
each assignment on a reader thread. PulseAudio enumerates named sources
(monitor/loopback sources are omitted). Sndio exposes its configured/default
capture endpoint when available. JACK enumerates source clients and connects
the selected source to the track.

JACK exposes each track's inputs as `<track> In L` / `<track> In R` ports for
optional routing in tools such as qpwgraph. The **External patchbay input**
choice is offered only there: other backends cannot leave a track port open
without a selected device, so that assignment is not accepted and does not
close the capture that is already open.

If the selected device fails to open, or its capture reader exits, recording
does not start a silently discarded empty take. The failure is reported through
the track input status and the existing unavailable-input error.

Shared synchronized recording and the final multi-track start/stop flow belong
to #50. Full surround routing remains in #63.
