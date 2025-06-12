add_executable(profiling
	src/main.c
)

target_include_directories(profiling PUBLIC ${PROJECT_SOURCE_DIR}/base/src)

target_link_libraries(profiling PRIVATE AzAudio)

set_target_properties(profiling PROPERTIES
	DEBUG_POSTFIX ${CMAKE_DEBUG_POSTFIX}
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
	VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
