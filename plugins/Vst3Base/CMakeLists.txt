if(NOT MXM_HAVE_VST3)
	RETURN()
ENDIF()

INCLUDE(BuildPlugin)

# Build the vendored Steinberg VST 3 SDK static libraries first (base,
# pluginterfaces, sdk_common, sdk, sdk_hosting).
ADD_SUBDIRECTORY(vst3sdk)

ADD_SUBDIRECTORY(vst3base)
