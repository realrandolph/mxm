# Thin build glue for the Steinberg VST 3 SDK.
#
# The SDK is vendored as four git submodules (base, pluginterfaces, public.sdk,
# cmake) pinned to the exact commits referenced by the v3.7.13_build_42 release.
# We intentionally do not add the SDK's own top-level CMakeLists.txt (which
# would also build the example plug-ins, validator, utilities, VSTGUI and the
# large documentation tree). Instead we replicate the small amount of setup it
# performs and add only the library targets we consume as a host:
#
#   base, pluginterfaces, sdk_common, sdk, sdk_hosting
#
# License: the vendored SDK components (base, pluginterfaces, public.sdk,
# cmake) are distributed under a BSD-3-Clause style license (see the LICENSE.txt
# in each submodule). This is compatible with MXM's GPL-2.0-or-later license.

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")

include(SMTG_VST3_SDK)

# Build only the libraries we need as a host; disable examples, utilities,
# VSTGUI and the validator.
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_ADD_VST3_UTILITIES OFF CACHE BOOL "" FORCE)

# Treat the SDK as third-party code so MXM's -Wall/-Werror flags (which the
# SDK does not build cleanly under, e.g. %lld vs int64 in ustring.cpp) do not
# apply to it.
set_directory_properties(PROPERTIES SYSTEM TRUE)

set(vst3sdk_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
smtg_enable_vst3_sdk()

# Enable the VST3 interface subset of pluginterfaces/public.sdk.
set(VST_SDK TRUE)

set(public_sdk_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/public.sdk")
set(pluginterfaces_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/pluginterfaces")

include_directories("${CMAKE_CURRENT_SOURCE_DIR}")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/public.sdk")

add_subdirectory(pluginterfaces)
add_subdirectory(base)
add_subdirectory(public.sdk)
