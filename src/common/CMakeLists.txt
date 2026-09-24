set(COMMON_SRCS
	RemotePluginBase.cpp
	SharedMemory.cpp
	SystemSemaphore.cpp
)

foreach(SRC ${COMMON_SRCS})
	list(APPEND MXM_COMMON_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/${SRC}")
endforeach()

set(MXM_COMMON_SRCS ${MXM_COMMON_SRCS} PARENT_SCOPE)
