cmake_minimum_required(VERSION 3.25)

set(CMAKE_DEBUG_POSTFIX _debug)

project(AzAudio)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)

if (NOT ${CMAKE_INSTALL_PREFIX})
	set(CMAKE_INSTALL_PREFIX $ENV{CMAKE_INSTALL_PREFIX})
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if (MSVC)
	add_compile_options(/W3)
else()
	add_compile_options(-Wall -Wno-missing-braces -fmax-errors=1)
	if (CMAKE_SYSTEM MATCHES Windows)
		add_compile_options(-D_GLIBCXX_USE_NANOSLEEP)
		add_link_options(-static-libgcc -static-libstdc++ -static -Llibwinpthread-1.dll)
	endif()
endif()

add_subdirectory(tools)

set(AZAUDIO_STATELESS_PARSER_EXECUTABLE_NAME
	$<IF:$<BOOL:$<CONFIG:Debug>>,statelessParser${CMAKE_DEBUG_POSTFIX}${CMAKE_EXECUTABLE_SUFFIX},statelessParser${CMAKE_EXECUTABLE_SUFFIX}>
)

file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/tools/${AZAUDIO_STATELESS_PARSER_EXECUTABLE_NAME}" PREFIX_PARSER_EXE)
file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/base/src/AzAudio/gui/prefixes.txt" PREFIX_PARSER_PREFIXES)
file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/base/src/AzAudio/gui/azaHasPrefix.gen.c" PREFIX_PARSER_OUTPUT)

add_custom_command(
	OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/base/src/AzAudio/gui/azaHasPrefix.gen.c
	COMMAND ${PREFIX_PARSER_EXE} --reverse ${PREFIX_PARSER_PREFIXES} -o ${PREFIX_PARSER_OUTPUT} azaHasPrefix
	MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/base/src/AzAudio/gui/prefixes.txt
	DEPENDS statelessParser
	COMMENT "Generating prefix parser..."
#	COMMENT "Generating prefix parser... \"${PREFIX_PARSER_EXE} --reverse ${PREFIX_PARSER_PREFIXES} -o ${PREFIX_PARSER_OUTPUT} azaHasPrefix\""
)
# In order to get it to remake azaHasPrefix.gen.c only when necessary, the only way to do that is to use both add_custom_command and add_custom_target together, because it just doesn't work otherwise... Getting CMake to behave is like pulling teeth sometimes
add_custom_target(AzAudio_prefix_parser
	DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/base/src/AzAudio/gui/azaHasPrefix.gen.c
)

add_subdirectory(base)

add_subdirectory(tests)