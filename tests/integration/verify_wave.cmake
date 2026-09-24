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

# RIFF + 4-byte size field + WAVE
if(NOT WAV_HEADER_HEX MATCHES "^52494646[0-9A-F]{8}57415645$")
	message(FATAL_ERROR "Rendered file is not a RIFF/WAVE file: ${WAV_HEADER_HEX}")
endif()

message(STATUS "Verified rendered WAV: ${WAV_FILE} (${WAV_SIZE} bytes)")
