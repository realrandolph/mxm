if(NOT MXM_HAVE_VST3)
	return()
endif()

INCLUDE(BuildPlugin)
INCLUDE_DIRECTORIES("${CMAKE_CURRENT_SOURCE_DIR}/../Vst3Base/vst3base")
LINK_DIRECTORIES("${CMAKE_CURRENT_BINARY_DIR}/..")
IF(MXM_BUILD_LINUX)
	LINK_LIBRARIES(-Wl,--enable-new-dtags)
	SET(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
	SET(CMAKE_INSTALL_RPATH "$ORIGIN")
ELSE()
	SET(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${PLUGIN_DIR}")
ENDIF()

BUILD_PLUGIN(vst3effect
	Vst3Effect.cpp
	Vst3EffectControls.cpp
	Vst3EffectControlDialog.cpp
	Vst3Effect.h
	Vst3EffectControls.h
	Vst3EffectControlDialog.h
	MOCFILES
		Vst3EffectControlDialog.h
		Vst3EffectControls.h
	EMBEDDED_RESOURCES *.svg
)
TARGET_LINK_LIBRARIES(vst3effect vst3base)
