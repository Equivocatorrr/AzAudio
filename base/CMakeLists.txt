if (CMAKE_SYSTEM MATCHES Windows)
	set(TARGET_PLATFORM_NAME Win32)
elseif (CMAKE_SYSTEM MATCHES Linux)
	set(TARGET_PLATFORM_NAME Linux)
endif()

# Configure Raylib to be as minimal as possible (we don't need much here)
set(CUSTOMIZE_BUILD ON)
set(OPENGL_VERSION 3.3)
set(USE_AUDIO OFF)
set(SUPPORT_MODULE_RMODELS OFF)
set(SUPPORT_MODULE_RAUDIO OFF)
set(SUPPORT_BUSY_WAIT_LOOP OFF)
set(SUPPORT_CAMERA_SYSTEM OFF)
set(SUPPORT_VR_SIMULATOR OFF)
set(SUPPORT_DISTORTION_SHADER OFF)
set(SUPPORT_IMAGE_EXPORT OFF)
set(SUPPORT_FILEFORMAT_PNG OFF)
set(SUPPORT_FILEFORMAT_GIF OFF)
set(SUPPORT_FILEFORMAT_QOI OFF)
set(SUPPORT_FILEFORMAT_DDS OFF)
set(SUPPORT_FILEFORMAT_BMP OFF)
set(SUPPORT_FILEFORMAT_JPG OFF)
set(SUPPORT_FILEFORMAT_OBJ OFF)
set(SUPPORT_FILEFORMAT_MTL OFF)
set(SUPPORT_FILEFORMAT_IQM OFF)
set(SUPPORT_FILEFORMAT_GLTF OFF)
set(SUPPORT_FILEFORMAT_VOX OFF)
set(SUPPORT_FILEFORMAT_M3D OFF)
set(SUPPORT_MESH_GENERATION OFF)
set(RL_SUPPORT_MESH_GPU_SKINNING OFF)
set(SUPPORT_FILEFORMAT_WAV OFF)
set(SUPPORT_FILEFORMAT_OGG OFF)
set(SUPPORT_FILEFORMAT_XM OFF)
set(SUPPORT_FILEFORMAT_MOD OFF)
set(SUPPORT_FILEFORMAT_MP3 ON)
set(SUPPORT_FILEFORMAT_QOA ON)
set(SUPPORT_SAVE_PNG OFF)

add_subdirectory(${PROJECT_SOURCE_DIR}/external/raylib ${PROJECT_BINARY_DIR}/external/raylib)

file(GLOB BACKEND_SOURCES "src/AzAudio/backend/${TARGET_PLATFORM_NAME}/*")

add_library(AzAudio STATIC
	src/AzAudio/AzAudio.h
	src/AzAudio/AzAudio.c
	src/AzAudio/channel_layout.h
	src/AzAudio/cpuid.h
	src/AzAudio/cpuid.c
	src/AzAudio/version.c
	src/AzAudio/error.h
	src/AzAudio/fft.h
	src/AzAudio/fft.c
	src/AzAudio/aza_c_std.h
	src/AzAudio/aza_c_std.c
	src/AzAudio/math.h
	src/AzAudio/math.c
	src/AzAudio/easing.h
	src/AzAudio/easing.c
	src/AzAudio/mixer.h
	src/AzAudio/mixer.c
	src/AzAudio/simd.h
	src/AzAudio/timer.h
	# gui
	src/AzAudio/gui/types.h
	src/AzAudio/gui/types.c
	src/AzAudio/gui/gui.h
	src/AzAudio/gui/gui.c
	src/AzAudio/gui/platform.h
	src/AzAudio/gui/platform-raylib.c
	src/AzAudio/mixer_gui.c
	# backend
	src/AzAudio/backend/backend.h
	src/AzAudio/backend/interface.h
	src/AzAudio/backend/interface.c
	src/AzAudio/backend/threads.h
	${BACKEND_SOURCES}
	# specialized implementations
	src/AzAudio/specialized/azaBufferDeinterlace.c
	src/AzAudio/specialized/azaBufferReinterlace.c
	src/AzAudio/specialized/azaBufferMixMatrix.c
	src/AzAudio/specialized/azaKernel.c
	# dsp basics
	src/AzAudio/dsp/dsp.h
	src/AzAudio/dsp/utility.h
	src/AzAudio/dsp/utility.c
	src/AzAudio/dsp/azaChannelMatrix.h
	src/AzAudio/dsp/azaChannelMatrix.c
	src/AzAudio/dsp/azaBuffer.h
	src/AzAudio/dsp/azaBuffer.c
	src/AzAudio/dsp/azaDSP.h
	src/AzAudio/dsp/azaDSP.c
	src/AzAudio/dsp/azaKernel.h
	src/AzAudio/dsp/azaKernel.c
	src/AzAudio/dsp/azaMeters.h
	src/AzAudio/dsp/azaMeters.c
	# plugins
	src/AzAudio/dsp/plugins/azaRMS.h
	src/AzAudio/dsp/plugins/azaRMS.c
	src/AzAudio/dsp/plugins/azaCubicLimiter.h
	src/AzAudio/dsp/plugins/azaCubicLimiter.c
	src/AzAudio/dsp/plugins/azaLookaheadLimiter.h
	src/AzAudio/dsp/plugins/azaLookaheadLimiter.c
	src/AzAudio/dsp/plugins/azaLowPassFIR.h
	src/AzAudio/dsp/plugins/azaLowPassFIR.c
	src/AzAudio/dsp/plugins/azaFilter.h
	src/AzAudio/dsp/plugins/azaFilter.c
	src/AzAudio/dsp/plugins/azaCompressor.h
	src/AzAudio/dsp/plugins/azaCompressor.c
	src/AzAudio/dsp/plugins/azaGate.h
	src/AzAudio/dsp/plugins/azaGate.c
	src/AzAudio/dsp/plugins/azaDelay.h
	src/AzAudio/dsp/plugins/azaDelay.c
	src/AzAudio/dsp/plugins/azaDelayDynamic.h
	src/AzAudio/dsp/plugins/azaDelayDynamic.c
	src/AzAudio/dsp/plugins/azaReverb.h
	src/AzAudio/dsp/plugins/azaReverb.c
	src/AzAudio/dsp/plugins/azaSampler.h
	src/AzAudio/dsp/plugins/azaSampler.c
	src/AzAudio/dsp/plugins/azaSpatialize.h
	src/AzAudio/dsp/plugins/azaSpatialize.c
	src/AzAudio/dsp/plugins/azaMonitorSpectrum.h
	src/AzAudio/dsp/plugins/azaMonitorSpectrum.c
)

set_target_properties(AzAudio PROPERTIES
	DEBUG_POSTFIX ${CMAKE_DEBUG_POSTFIX}
)

target_include_directories(AzAudio PUBLIC
	$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/base/src>
	$<INSTALL_INTERFACE:include>
)

# target_include_directories(AzAudio PRIVATE ${PROJECT_SOURCE_DIR}/external/raylib/src)
target_link_libraries(AzAudio PRIVATE raylib)

if (CMAKE_SYSTEM MATCHES Windows)
	target_link_libraries(AzAudio INTERFACE ksuser Winmm)
	target_compile_definitions(AzAudio PRIVATE _CRT_SECURE_NO_WARNINGS)
	set_property(TARGET AzAudio PROPERTY C_STANDARD 11)
elseif (CMAKE_SYSTEM MATCHES Linux)
	target_link_libraries(AzAudio INTERFACE pthread)

	find_package(PkgConfig REQUIRED)
	pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)
	target_include_directories(AzAudio PRIVATE ${PIPEWIRE_INCLUDE_DIRS})
	target_compile_options(AzAudio PRIVATE ${PIPEWIRE_CFLAGS_OTHER})
endif()

# installation

install(TARGETS AzAudio
	EXPORT AzAudio
	CONFIGURATIONS Debug Release
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}/AzAudio
	INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/AzAudio
)
install(EXPORT AzAudio
	FILE AzAudio.cmake
	NAMESPACE AzAudio::
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AzAudio
)
install(FILES AzAudioConfig.cmake
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AzAudio
)
install(DIRECTORY src/AzAudio
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
	FILES_MATCHING PATTERN "*.h"
)

# TODO: Figure out the actual proper way to do this since I'm like 80% sure this isn't enough

install(TARGETS raylib
	EXPORT raylib
	CONFIGURATIONS Debug Release
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}/AzAudio/raylib
	INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/AzAudio/raylib
	PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/AzAudio/raylib
)
install(EXPORT raylib
	FILE raylib.cmake
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AzAudio/raylib
)

install(TARGETS glfw
	EXPORT glfw
	CONFIGURATIONS Debug Release
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}/AzAudio/raylib/glfw
	INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/AzAudio/raylib/glfw
	PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/AzAudio/raylib/glfw
)
install(EXPORT glfw
	FILE glfw.cmake
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AzAudio/raylib/glfw
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES SOURCE_LIST)