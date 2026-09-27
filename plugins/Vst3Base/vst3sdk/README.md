# Steinberg VST 3 SDK (vendored)

This directory vendors the components of the [Steinberg VST 3 SDK]
(https://github.com/steinbergmedia/vst3sdk) required to host VST 3 plug-ins.

Pinned to the commits tagged as the `v3.8.1_build_84` release:

| Directory        | Upstream repository                                      | Commit                                    |
| ---------------- | -------------------------------------------------------- | ----------------------------------------- |
| `base`           | https://github.com/steinbergmedia/vst3_base              | `fcf9da0bd27a16f7f03773a3a39822f28f5c8477` |
| `pluginterfaces` | https://github.com/steinbergmedia/vst3_pluginterfaces    | `4f547e8e102b47de4a8b8aaf343c73b700786372` |
| `public.sdk`     | https://github.com/steinbergmedia/vst3_public_sdk        | `586dc5e6c8012c3e4b01c79389375cbe96bdb1da` |
| `cmake`          | https://github.com/steinbergmedia/vst3_cmake             | `054c9143cbb8d47fc4694e473f2ee3b4d951a8f5` |

## License

All four components are distributed under the MIT License (see `LICENSE.txt`
in each subdirectory), which is compatible with MXM's GPL-2.0-or-later
license.

The `VST` and `VST3` trademarks and the "VST 3" name are owned by Steinberg
Media Technologies GmbH. MXM uses these names only to refer to the plugin
format and does not claim any endorsement by Steinberg.

## Build

`CMakeLists.txt` in this directory adds only the static libraries required for
hosting (`base`, `pluginterfaces`, `sdk_common`, `sdk`, `sdk_hosting`) and
disables the SDK's examples, validator, utilities and VSTGUI. This keeps the
build small and avoids pulling in the SDK's auxiliary dependencies.
