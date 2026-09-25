if(NOT MXM_HAVE_VST3)
	RETURN()
ENDIF()

INCLUDE(BuildPlugin)

# Build the vendored Steinberg VST 3 SDK static libraries first (base,
# pluginterfaces, sdk_common, sdk_hosting).
ADD_SUBDIRECTORY(vst3sdk)

# The SDK's sdk_hosting library ships the generic Module class but not the
# platform-specific module loader; add it explicitly.
IF(MXM_BUILD_LINUX)
	SET(VST3_PLATFORM_MODULE_SOURCE "../vst3sdk/public.sdk/source/vst/hosting/module_linux.cpp")
ELSEIF(MXM_BUILD_WIN32)
	SET(VST3_PLATFORM_MODULE_SOURCE "../vst3sdk/public.sdk/source/vst/hosting/module_win32.cpp")
ENDIF()

ADD_SUBDIRECTORY(vst3base)
