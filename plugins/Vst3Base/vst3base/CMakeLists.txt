BUILD_PLUGIN(vst3base
	vst3_base.cpp
	Vst3Plugin.cpp Vst3Plugin.h
	Vst3Manager.cpp Vst3Manager.h
	Vst3SubPluginFeatures.cpp Vst3SubPluginFeatures.h
	../vst3sdk/public.sdk/source/common/memorystream.cpp
	EXPORT_BASE_NAME vst3base
	LINK SHARED
)

TARGET_INCLUDE_DIRECTORIES(vst3base
	PUBLIC ../
)

TARGET_LINK_LIBRARIES(vst3base
	sdk_hosting
	sdk
)

IF(MXM_BUILD_LINUX)
	TARGET_LINK_LIBRARIES(vst3base dl pthread)
ENDIF()
