add_executable(unit_tests
	src/main.c
	src/testing.c
	src/testing.h
	src/vt_strings.h
	# tests
	src/tests/azaBufferResize.c
)

target_include_directories(unit_tests PUBLIC ${PROJECT_SOURCE_DIR}/base/src)

target_link_libraries(unit_tests PRIVATE AzAudio)

set_target_properties(unit_tests PROPERTIES
	DEBUG_POSTFIX ${CMAKE_DEBUG_POSTFIX}
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
	VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
