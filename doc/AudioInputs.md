# Per-track audio inputs

Every recordable Sample Track owns its own capture assignment. On SDL, each
track's **Input** dropdown selects an enumerated capture device and opens that
device for the track. On JACK, it selects a named source client. Neither needs
a global recording-input choice or patchbay setup for ordinary use. Backends
that expose only one capture stream provide per-track channel assignments from
that stream.

## Assignment model

`mxm::AudioInputSource` describes one selectable endpoint:

- stable `id`;
- human-readable `displayName`;
- source `channels` and `AudioChannelLayout`;
- `captureChannels`, mapping each source channel to the shared capture snapshot;
- `trackPort` and `endpointPorts` for backend-routed per-track sources.

The track stores `input_source` and an optional `input_channels` list in the
project. The channel list makes nonadjacent and future multichannel mappings
explicit; it is not inferred from a stereo assumption. Projects without these
attributes load without an assigned source.

Backends with a shared capture stream expose sources such as:

- `auto`: all configured capture channels;
- one mono source per hardware channel;
- one adjacent stereo-pair source per pair, for example `Inputs 3 + 4`.

The list is rebuilt from the active backend and capture layout. A saved
assignment that no longer exists is shown as unavailable. Recording is refused
with a visible error instead of silently falling back to another source.

## Destination policy

Physical input selection is separate from the existing **Record to** control:

- **All channels (Auto)** captures the selected source's own channels/layout;
- **Left channel only** / **Right channel only** route the selected source into
  the left or right destination channel of a stereo take.

`SampleCapture` supports an explicit source-to-destination map so channels can
be reordered or duplicated without changing the shared capture boundary.

## Monitoring, metering and triggering

Monitoring, the track input meter and input-triggered recording use the
assigned source channels. They do not read the engine-wide input peak. If an
assignment becomes unavailable during a take, the take is not remapped: the
track records silence and reports the problem when recording stops.

## Backend behavior

Backends can either provide per-track capture endpoints or share one capture
stream. SDL opens the selected device independently for each assigned track;
PortAudio currently shares its configured capture stream and offers channel
assignments from it.

When a backend supports independently routable track ports
(`AudioDevice::supportsTrackInputPorts()`), a recordable track gets its own
backend input ports. JACK lists external source clients in the track's **Input**
dropdown and connects the chosen source to that track. It also exposes each
track's inputs as `<track> In L` / `<track> In R` ports for optional routing in
tools such as qpwgraph.

Shared synchronized recording and the final multi-track start/stop flow belong
to #50. Full surround routing remains in #63; this input model already carries
arbitrary channel maps so it can feed those layouts without a redesign.
