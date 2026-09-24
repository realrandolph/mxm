set(MXM_INTEGRATION_ARTIFACT_DIR "${CMAKE_CURRENT_BINARY_DIR}/artifacts")
file(MAKE_DIRECTORY "${MXM_INTEGRATION_ARTIFACT_DIR}")

# MXM is a GUI-subsystem executable on release Windows builds. --help and
# --version return before the Windows console attach path, so their portable
# contract here is a clean exit rather than captured stdout.
add_test(NAME IntegrationCliVersion COMMAND mxm --version)
set_tests_properties(IntegrationCliVersion PROPERTIES
	LABELS "integration;smoke"
	TIMEOUT 15
)

add_test(NAME IntegrationCliHelp COMMAND mxm --help)
set_tests_properties(IntegrationCliHelp PROPERTIES
	LABELS "integration;smoke"
	TIMEOUT 15
)

set(MXM_EMPTY_PROJECT_WAV "${MXM_INTEGRATION_ARTIFACT_DIR}/emptyproject.wav")

add_test(
	NAME IntegrationRenderEmptyProject
	COMMAND mxm render "${CMAKE_SOURCE_DIR}/tests/emptyproject.mmp"
		--output "${MXM_EMPTY_PROJECT_WAV}"
)
set_tests_properties(IntegrationRenderEmptyProject PROPERTIES
	LABELS "integration;render"
	FIXTURES_SETUP empty_project_render
	TIMEOUT 120
)

add_test(
	NAME IntegrationVerifyEmptyProjectRender
	COMMAND "${CMAKE_COMMAND}"
		-DWAV_FILE=${MXM_EMPTY_PROJECT_WAV}
		-P "${CMAKE_CURRENT_SOURCE_DIR}/verify_wave.cmake"
)
set_tests_properties(IntegrationVerifyEmptyProjectRender PROPERTIES
	LABELS "integration;render"
	FIXTURES_REQUIRED empty_project_render
	TIMEOUT 15
)
