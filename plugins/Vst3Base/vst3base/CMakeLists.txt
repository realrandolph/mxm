BUILD_PLUGIN(vst3base
	vst3_base.cpp
	Vst3Plugin.cpp Vst3Plugin.h
	Vst3Manager.cpp Vst3Manager.h
	Vst3SubPluginFeatures.cpp Vst3SubPluginFeatures.h
	../vst3sdk/public.sdk/source/common/memorystream.cpp
	${VST3_PLATFORM_MODULE_SOURCE}
	EXPORT_BASE_NAME vst3base
	LINK SHARED
)

TARGET_INCLUDE_DIRECTORIES(vst3base
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

TARGET_LINK_LIBRARIES(vst3base
	sdk_hosting
)

IF(MXM_BUILD_LINUX)
	TARGET_LINK_LIBRARIES(vst3base dl pthread)
ELSEIF(MXM_BUILD_WIN32)
	TARGET_LINK_LIBRARIES(vst3base ole32 shell32)
ENDIF()
