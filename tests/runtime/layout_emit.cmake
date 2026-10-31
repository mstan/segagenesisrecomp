# The fixture is a host executable used during the build. Keep cross builds
# independent of host execution/emulation; their normal source builds remain.
if(CMAKE_CROSSCOMPILING)
    return()
endif()
set(_layout_root "${CMAKE_CURRENT_LIST_DIR}/../..")
add_executable(layout_emit_fixture "${CMAKE_CURRENT_LIST_DIR}/layout_emit_fixture.c"
    "${_layout_root}/recompiler/src/game_config.c" "${_layout_root}/recompiler/src/toml.c")
target_include_directories(layout_emit_fixture PRIVATE "${_layout_root}/recompiler/src")
target_compile_features(layout_emit_fixture PRIVATE c_std_11)
if(MSVC)
    target_compile_definitions(layout_emit_fixture PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
set(_layout_outputs)
foreach(_kind IN ITEMS empty filled)
    set(_output "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/layout-${_kind}.c")
    add_custom_command(OUTPUT "${_output}"
        COMMAND $<TARGET_FILE:layout_emit_fixture> ${_kind} "${_output}"
        DEPENDS layout_emit_fixture VERBATIM)
    set_source_files_properties("${_output}" PROPERTIES
        COMPILE_DEFINITIONS "g_game_layout=${_kind}_game_layout")
    list(APPEND _layout_outputs "${_output}")
endforeach()
add_executable(layout_generated_test "${CMAKE_CURRENT_LIST_DIR}/layout_generated_test.c" ${_layout_outputs})
target_include_directories(layout_generated_test PRIVATE "${_layout_root}/runner")
target_compile_features(layout_generated_test PRIVATE c_std_11)
set_target_properties(layout_generated_test PROPERTIES C_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(layout_generated_test PRIVATE /W4 /WX)
else()
    target_compile_options(layout_generated_test PRIVATE -Wall -Wextra -Werror -pedantic-errors)
endif()
add_test(NAME layout_generated COMMAND layout_generated_test)
