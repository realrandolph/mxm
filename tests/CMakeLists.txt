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
