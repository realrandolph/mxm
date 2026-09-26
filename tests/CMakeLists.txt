include(CTest)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

set(MXM_TESTS
	src/core/ArrayVectorTest.cpp
	src/core/AudioBufferTest.cpp
	src/core/AutomatableModelTest.cpp
	src/core/MathTest.cpp
	src/core/ProjectVersionTest.cpp
	src/core/RelativePathsTest.cpp
	src/core/TimelineTest.cpp
	src/tracks/AutomationTrackTest.cpp
)

foreach(MXM_TEST_SRC IN LISTS MXM_TESTS)
	# TODO CMake 3.20: Use cmake_path
	get_filename_component(MXM_TEST_NAME ${MXM_TEST_SRC} NAME_WE)

	add_executable(${MXM_TEST_NAME} ${MXM_TEST_SRC})
	add_test(NAME ${MXM_TEST_NAME} COMMAND ${MXM_TEST_NAME})
	set_tests_properties(${MXM_TEST_NAME} PROPERTIES LABELS "unit")

	# TODO CMake 3.12: Propagate usage requirements by linking to mxmobjs
	target_include_directories(${MXM_TEST_NAME} PRIVATE $<TARGET_PROPERTY:mxmobjs,INCLUDE_DIRECTORIES>)

	target_static_libraries("${MXM_TEST_NAME}" PRIVATE mxmobjs)
	target_link_libraries(${MXM_TEST_NAME} PRIVATE
		${QT_LIBRARIES}
		${QT_QTTEST_LIBRARY}
	)

	target_compile_features(${MXM_TEST_NAME} PRIVATE cxx_std_20)
	target_compile_definitions(${MXM_TEST_NAME} PRIVATE MXM_TESTING)
endforeach()

# Native VST3 host adapter test (no GUI session required).
if(MXM_HAVE_VST3 AND TARGET vst3base)
	add_executable(Vst3AdapterTest src/core/Vst3AdapterTest.cpp)
	add_test(NAME Vst3AdapterTest COMMAND Vst3AdapterTest)
	set_tests_properties(Vst3AdapterTest PROPERTIES
		LABELS "unit;vst3"
		TIMEOUT 300
	)
	target_include_directories(Vst3AdapterTest PRIVATE ${CMAKE_SOURCE_DIR}/include)
	target_link_libraries(Vst3AdapterTest PRIVATE vst3base Qt${QT_VERSION_MAJOR}::Core)
	target_compile_features(Vst3AdapterTest PRIVATE cxx_std_20)
	# End-to-end render test through the mxm CLI (no GUI session required).
	add_executable(Vst3RenderTest src/core/Vst3RenderTest.cpp)
	add_test(NAME Vst3RenderTest COMMAND Vst3RenderTest $<TARGET_FILE:mxm>)
	set_tests_properties(Vst3RenderTest PROPERTIES
		LABELS "integration;render;vst3"
		TIMEOUT 300
	)
	target_include_directories(Vst3RenderTest PRIVATE ${CMAKE_SOURCE_DIR}/include)
	target_link_libraries(Vst3RenderTest PRIVATE vst3base Qt${QT_VERSION_MAJOR}::Core)
	target_compile_features(Vst3RenderTest PRIVATE cxx_std_20)
	if(MXM_BUILD_LINUX)
		set_tests_properties(Vst3AdapterTest Vst3RenderTest PROPERTIES
			ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR}/plugins:$ENV{LD_LIBRARY_PATH}")
	elseif(MXM_BUILD_WIN32)
		add_custom_command(TARGET Vst3AdapterTest POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				$<TARGET_FILE:vst3base> $<TARGET_FILE_DIR:Vst3AdapterTest>)
		add_custom_command(TARGET Vst3RenderTest POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				$<TARGET_FILE:vst3base> $<TARGET_FILE_DIR:Vst3RenderTest>)
	endif()

	# Third-party modules may not support concurrent initialization from
	# separate processes. Keep the tests sharing VST3_PATH from overlapping.
	set_tests_properties(Vst3AdapterTest Vst3RenderTest PROPERTIES
		RESOURCE_LOCK vst3_test_plugins)
endif()

add_subdirectory(integration)
