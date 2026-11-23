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
capture endpoint when available. JACK enumerates each source port as a mono
choice and every pair of ports as a stereo choice, including non-adjacent
pairs, then connects the selected channels to the track.

JACK exposes eight inputs per track (`<track> In 1` through `<track> In 8`)
for optional routing in tools such as qpwgraph. The **External patchbay input**
choice is offered only there: other backends cannot leave a track port open
without a selected device, so that assignment is not accepted and does not
close the capture that is already open. The eight JACK capture channels allow
independent mono-track assignments, stereo pairs, and mixed mono/stereo
assignments from a multichannel source.

If the selected device fails to open, or its capture reader exits, recording
does not start a silently discarded empty take. The failure is reported through
the track input status and the existing unavailable-input error.

Capture blocks must match the registered channel count. A mismatched block is
rejected in full and counted as dropped frames; mono is not duplicated and
extra channels are not silently truncated.

Shared synchronized recording and the final multi-track start/stop flow belong
to #50. Full surround routing remains in #63.
