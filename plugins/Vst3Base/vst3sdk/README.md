# Steinberg VST 3 SDK (vendored)

This directory vendors the components of the [Steinberg VST 3 SDK]
(https://github.com/steinbergmedia/vst3sdk) required to host VST 3 plug-ins.

Pinned to the commits referenced by the `v3.7.13_build_42` release:

| Directory      | Upstream repository                                   | Commit   |
| -------------- | ----------------------------------------------------- | -------- |
| `base`         | https://github.com/steinbergmedia/vst3_base           | `823de87` |
| `pluginterfaces` | https://github.com/steinbergmedia/vst3_pluginterfaces | `0786cbe` |
| `public.sdk`   | https://github.com/steinbergmedia/vst3_public_sdk     | `6ccc102` |
| `cmake`        | https://github.com/steinbergmedia/vst3_cmake          | `49af690` |

## License

These components are distributed under a BSD-3-Clause style license (see
`LICENSE.txt` in each subdirectory), which is compatible with MXM's
GPL-2.0-or-later license.

The `VST` and `VST3` trademarks and the "VST 3" name are owned by Steinberg
Media Technologies GmbH. MXM uses these names only to refer to the plugin
format and does not claim any endorsement by Steinberg.

## Build

`CMakeLists.txt` in this directory adds only the static libraries required for
hosting (`base`, `pluginterfaces`, `sdk_common`, `sdk`, `sdk_hosting`) and
disables the SDK's examples, validator, utilities and VSTGUI. This keeps the
build small and avoids pulling in the SDK's auxiliary dependencies.
