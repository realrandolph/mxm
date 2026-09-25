ADD_SUBDIRECTORY(3rdparty)

CONFIGURE_FILE("mxmconfig.h.in"        "${CMAKE_BINARY_DIR}/mxmconfig.h")

# Provide config flags to mxmversion.h
get_cmake_property(_define_vars VARIABLES)
foreach (_define_var ${_define_vars})
    if(_define_var MATCHES "^WANT|MXM_(HAVE|DEBUG)" )
        list(APPEND MXM_BUILD_OPTIONS "${_define_var}='${${_define_var}}'")
    endif()
endforeach()

# Format for readability
string(REPLACE ";" " " MXM_BUILD_OPTIONS "${MXM_BUILD_OPTIONS}")

CONFIGURE_FILE("mxmversion.h.in"       "${CMAKE_BINARY_DIR}/mxmversion.h")

SET(MXM_SRCS "")

SET(CMAKE_AUTOMOC ON)
SET(CMAKE_AUTOUIC ON)
SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Enable C++20
SET(CMAKE_CXX_STANDARD 20)

IF(MXM_BUILD_APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
ENDIF()

ADD_SUBDIRECTORY(common)
ADD_SUBDIRECTORY(core)
ADD_SUBDIRECTORY(gui)
ADD_SUBDIRECTORY(tracks)

LIST(APPEND MXM_SRCS ${MXM_COMMON_SRCS})

IF(WANT_QT6)
	QT6_WRAP_UI(MXM_UI_OUT ${MXM_UIS})
ELSE()
	QT5_WRAP_UI(MXM_UI_OUT ${MXM_UIS})
ENDIF()

INCLUDE_DIRECTORIES(
	"${CMAKE_CURRENT_BINARY_DIR}"
	"${CMAKE_BINARY_DIR}"
	"${CMAKE_BINARY_DIR}/include"
	"${CMAKE_SOURCE_DIR}"
	"${CMAKE_SOURCE_DIR}/include"
)

IF(WIN32 AND MSVC)
	SET(WINRC "${CMAKE_BINARY_DIR}/mxm.rc")
ELSEIF(WIN32)
	SET(WINRC "${CMAKE_BINARY_DIR}/mxmrc.obj")
	ADD_CUSTOM_COMMAND(OUTPUT "${WINRC}"
				COMMAND "${CMAKE_RC_COMPILER}"
					"-I\"${CMAKE_SOURCE_DIR}\""
					"-o\"${CMAKE_BINARY_DIR}/mxmrc.obj\""
					"-i\"${CMAKE_BINARY_DIR}/mxm.rc\""
				DEPENDS "${CMAKE_BINARY_DIR}/mxm.rc")
ENDIF()

INCLUDE(GenQrc)
ADD_GEN_QRC(MXM_RCC_OUT mxm.qrc
	"${CMAKE_SOURCE_DIR}/doc/AUTHORS"
	"${CMAKE_SOURCE_DIR}/LICENSE.txt"
	"${CONTRIBUTORS}"
)

# Paths relative to mxm executable
FILE(RELATIVE_PATH LIB_DIR_RELATIVE "/${BIN_DIR}" "/${LIB_DIR}")
FILE(RELATIVE_PATH PLUGIN_DIR_RELATIVE "/${BIN_DIR}" "/${PLUGIN_DIR}")
ADD_DEFINITIONS(-DLIB_DIR="${LIB_DIR_RELATIVE}" -DPLUGIN_DIR="${PLUGIN_DIR_RELATIVE}" ${PULSEAUDIO_DEFINITIONS})
include_directories(SYSTEM
	${JACK_INCLUDE_DIRS}
	${SNDIO_INCLUDE_DIRS}
	${FFTW3F_INCLUDE_DIRS}
)

IF(PULSEAUDIO_INCLUDE_DIR)
	include_directories(SYSTEM "${PULSEAUDIO_INCLUDE_DIR}")
ENDIF()

IF(LV2_INCLUDE_DIRS)
	include_directories(SYSTEM ${LV2_INCLUDE_DIRS})
ENDIF()

IF(Lilv_INCLUDE_DIRS)
	include_directories(SYSTEM ${Lilv_INCLUDE_DIRS})
ENDIF()

IF(SUIL_INCLUDE_DIRS)
	include_directories(SYSTEM ${SUIL_INCLUDE_DIRS})
ENDIF()

# Use libraries in non-standard directories (e.g., another version of Qt)
IF(MXM_BUILD_LINUX)
	LINK_LIBRARIES(-Wl,--enable-new-dtags)
	SET(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
ENDIF()
SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

ADD_LIBRARY(mxmobjs OBJECT
	${MXM_SRCS}
	${MXM_INCLUDES}
	${MXM_RCC_OUT}
)

GENERATE_EXPORT_HEADER(mxmobjs
	BASE_NAME mxm
)

ADD_EXECUTABLE(mxm
	core/main.cpp
	"${WINRC}"
)
TARGET_INCLUDE_DIRECTORIES(mxm
	PUBLIC ${CMAKE_CURRENT_BINARY_DIR}
)
target_static_libraries(mxm PUBLIC mxmobjs)

# Enable updated C++ language standards
IF(MSVC)
	TARGET_COMPILE_OPTIONS(mxmobjs PUBLIC "/Zc:__cplusplus" "/permissive-")
ENDIF()

# CMake doesn't define target_EXPORTS for OBJECT libraries.
# See the documentation of DEFINE_SYMBOL for details.
# Also add MXM_STATIC_DEFINE for targets linking against it.
TARGET_COMPILE_DEFINITIONS(mxmobjs
	PRIVATE -Dmxmobjs_EXPORTS
)
target_static_definitions(mxmobjs MXM_STATIC_DEFINE)

# Set Visual Studio startup project to mxm
# https://stackoverflow.com/a/37994396/8166701
IF(NOT CMAKE_VERSION VERSION_LESS 3.6)
	SET_PROPERTY(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT mxm)
ENDIF()

SET_DIRECTORY_PROPERTIES(PROPERTIES ADDITIONAL_MAKE_CLEAN_FILES "${MXM_RCC_OUT} mxmconfig.h")

IF(MXM_HAVE_WINMM)
	SET(EXTRA_LIBRARIES "winmm")
ENDIF()

IF(MXM_BUILD_APPLE)
	SET(EXTRA_LIBRARIES "-framework CoreMIDI -framework CoreFoundation")
ENDIF()

IF(MXM_BUILD_HAIKU)
	SET(EXTRA_LIBRARIES "-lnetwork")
ENDIF()

if(MXM_HAVE_LIBRT)
	list(APPEND EXTRA_LIBRARIES "rt")
endif()

if(MXM_HAVE_PORTAUDIO)
	list(APPEND EXTRA_LIBRARIES portaudio::portaudio)
endif()

if(MXM_HAVE_MP3LAME)
	list(APPEND EXTRA_LIBRARIES mp3lame::mp3lame)
endif()

if(MXM_HAVE_OGGVORBIS)
	list(APPEND EXTRA_LIBRARIES Vorbis::vorbisenc Vorbis::vorbisfile)
endif()

if(MXM_HAVE_LV2)
	list(APPEND EXTRA_LIBRARIES Lilv::lilv)
endif()

if(TARGET qx11embedcontainer)
	list(APPEND EXTRA_LIBRARIES qx11embedcontainer)
	target_compile_definitions(mxmobjs PRIVATE MXM_HAVE_X11_EMBED_CONTAINER)
endif()

SET(MXM_REQUIRED_LIBS ${MXM_REQUIRED_LIBS}
	${CMAKE_THREAD_LIBS_INIT}
	${QT_LIBRARIES}
	${ASOUND_LIBRARY}
	${SDL_LIBRARY}
	${SDL2_LIBRARY}
	${SNDIO_LIBRARIES}
	${PULSEAUDIO_LIBRARIES}
	${JACK_LIBRARIES}
	${SUIL_LIBRARIES}
	${FFTW3F_LIBRARIES}
	SampleRate::samplerate
	SndFile::sndfile
	${EXTRA_LIBRARIES}
)

target_link_libraries(mxmobjs
	${MXM_REQUIRED_LIBS}
)
target_static_libraries(mxmobjs ringbuffer)
if(MXM_HAVE_LV2_UI)
	target_include_directories(mxmobjs SYSTEM PRIVATE ${X11_INCLUDE_DIR})
	target_link_libraries(mxmobjs ${X11_LIBRARIES})
endif()

set_target_properties(mxm PROPERTIES
	ENABLE_EXPORTS ON
	WIN32_EXECUTABLE $<NOT:$<CONFIG:DEBUG>>
)

set_target_properties(mxmobjs PROPERTIES AUTOUIC_SEARCH_PATHS "gui/modals")

IF(NOT WIN32 AND NOT MXM_BUILD_APPLE)
	if(CMAKE_INSTALL_MANDIR)
		SET(INSTALL_MANDIR ${CMAKE_INSTALL_MANDIR})
	ELSE(CMAKE_INSTALL_MANDIR)
		SET(INSTALL_MANDIR share/man)
	ENDIF(CMAKE_INSTALL_MANDIR)
	INSTALL(FILES "${CMAKE_SOURCE_DIR}/doc/mxm.1"
			DESTINATION "${INSTALL_MANDIR}/man1/"
			PERMISSIONS OWNER_READ GROUP_READ WORLD_READ)
ENDIF()

INSTALL(TARGETS mxm RUNTIME DESTINATION "${BIN_DIR}")
