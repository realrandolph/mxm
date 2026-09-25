if(NOT MXM_HAVE_VST3)
	return()
endif()

include(BuildPlugin)
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../Vst3Base/vst3base")
link_directories("${CMAKE_CURRENT_BINARY_DIR}/..")
if(MXM_BUILD_LINUX)
	link_libraries(-Wl,--enable-new-dtags)
	set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
	set(CMAKE_INSTALL_RPATH "$ORIGIN")
else()
	set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${PLUGIN_DIR}")
endif()

build_plugin(vst3instrument
	Vst3Instrument.cpp
	Vst3InstrumentView.cpp
	Vst3Instrument.h
	Vst3InstrumentView.h
	MOCFILES
		Vst3InstrumentView.h
	EMBEDDED_RESOURCES *.svg
)
target_link_libraries(vst3instrument vst3base)
