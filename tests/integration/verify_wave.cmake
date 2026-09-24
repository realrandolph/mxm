if(NOT DEFINED WAV_FILE)
	message(FATAL_ERROR "WAV_FILE was not provided")
endif()

if(NOT EXISTS "${WAV_FILE}")
	message(FATAL_ERROR "Expected rendered WAV does not exist: ${WAV_FILE}")
endif()

file(SIZE "${WAV_FILE}" WAV_SIZE)
if(WAV_SIZE LESS 44)
	message(FATAL_ERROR "Rendered WAV is too small to contain a valid header: ${WAV_SIZE} bytes")
endif()

file(READ "${WAV_FILE}" WAV_HEADER_HEX OFFSET 0 LIMIT 12 HEX)
string(TOUPPER "${WAV_HEADER_HEX}" WAV_HEADER_HEX)
string(SUBSTRING "${WAV_HEADER_HEX}" 0 8 RIFF_MAGIC)
string(SUBSTRING "${WAV_HEADER_HEX}" 16 8 WAVE_MAGIC)

if(NOT RIFF_MAGIC STREQUAL "52494646" OR NOT WAVE_MAGIC STREQUAL "57415645")
	message(FATAL_ERROR "Rendered file is not a RIFF/WAVE file: ${WAV_HEADER_HEX}")
endif()

message(STATUS "Verified rendered WAV: ${WAV_FILE} (${WAV_SIZE} bytes)")
