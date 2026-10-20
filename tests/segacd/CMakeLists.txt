cmake_minimum_required(VERSION 3.16)
project(SegaCDDeviceTests C)
enable_testing()
add_executable(segacd_memory_test memory_test.c ../../runner/cd/segacd.c)
target_include_directories(segacd_memory_test PRIVATE ../../runner/cd)
target_compile_features(segacd_memory_test PRIVATE c_std_11)
if(MSVC)
  target_compile_options(segacd_memory_test PRIVATE /W4)
else()
  target_compile_options(segacd_memory_test PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME segacd_memory COMMAND segacd_memory_test)

# The host generator and its compiled synthetic output validate the new shared
# step-emission seam. Cross builds still compile the portable device tests;
# generated execution tests need a runnable host toolchain.
if(NOT CMAKE_CROSSCOMPILING)
  get_filename_component(engine_root "../.." ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  include("${engine_root}/external/m68k-recomp-core/cmake/M68kRecompCore.cmake")
  m68k_recomp_core_sources(step_sources genesis)
  m68k_recomp_core_include_dirs(step_includes genesis)
  m68k_recomp_step_sources(step_runtime_sources)
  m68k_recomp_step_include_dirs(step_runtime_includes)
  add_executable(segacd_step_emit_test step_emit_test.c ${step_sources}
    "${engine_root}/recompiler/src/rom_parser.c"
    "${engine_root}/recompiler/src/game_config.c"
    "${engine_root}/recompiler/src/toml.c")
  target_include_directories(segacd_step_emit_test PRIVATE ${step_includes} "${engine_root}/recompiler/src")
  target_compile_features(segacd_step_emit_test PRIVATE c_std_11)
  set(step_output "${CMAKE_CURRENT_BINARY_DIR}/step_semantics_generated.c")
  add_custom_command(OUTPUT "${step_output}"
    COMMAND segacd_step_emit_test "${step_output}"
    DEPENDS segacd_step_emit_test VERBATIM)
  add_executable(segacd_step_semantics_test "${step_output}" ${step_runtime_sources})
  target_include_directories(segacd_step_semantics_test PRIVATE ${step_runtime_includes})
  target_compile_features(segacd_step_semantics_test PRIVATE c_std_11)
  add_test(NAME segacd_step_semantics COMMAND segacd_step_semantics_test)
  add_executable(segacd_floor_divide_test floor_divide_test.c
    "${engine_root}/runner/m68k_interp.c"
    "${engine_root}/external/m68k-recomp-core/common/m68k_decoder.c"
    "${engine_root}/recompiler/src/rom_parser.c")
  target_include_directories(segacd_floor_divide_test PRIVATE ${step_includes}
    "${engine_root}/runner" "${engine_root}/runner/include" "${engine_root}/recompiler/src")
  target_compile_features(segacd_floor_divide_test PRIVATE c_std_11)
  add_test(NAME segacd_floor_divide COMMAND segacd_floor_divide_test)
  add_subdirectory("${engine_root}/external/m68k-recomp-core/tests/step" "core-step")
endif()
