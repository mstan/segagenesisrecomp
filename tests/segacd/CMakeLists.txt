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
  add_executable(segacd_module_fixture module_fixture.c)
  add_executable(segacd_module_recompiler "${engine_root}/recompiler/src/main_step.c"
    ${step_sources} "${engine_root}/recompiler/src/rom_parser.c"
    "${engine_root}/recompiler/src/game_config.c" "${engine_root}/recompiler/src/toml.c")
  target_include_directories(segacd_module_recompiler PRIVATE ${step_includes} "${engine_root}/recompiler/src")
  target_compile_features(segacd_module_recompiler PRIVATE c_std_11)
  set(module_dir "${CMAKE_CURRENT_BINARY_DIR}/module-fixtures")
  file(MAKE_DIRECTORY "${module_dir}")
  set(module_sources)
  foreach(module IN ITEMS fixture_a fixture_b)
    list(APPEND module_sources "${module_dir}/${module}_module.c" "${module_dir}/${module}.h")
    foreach(part RANGE 0 31)
      if(part LESS 10)
        set(part "0${part}")
      endif()
      list(APPEND module_sources "${module_dir}/${module}_part${part}.c")
    endforeach()
  endforeach()
  add_custom_command(OUTPUT ${module_sources}
    COMMAND segacd_module_fixture "${module_dir}/a.bin" "${module_dir}/b.bin"
    COMMAND segacd_module_recompiler "${module_dir}/a.bin" "${module_dir}" fixture_a 0x1000 0x1000
    COMMAND segacd_module_recompiler "${module_dir}/b.bin" "${module_dir}" fixture_b 0x1000 --all
    DEPENDS segacd_module_fixture segacd_module_recompiler VERBATIM)
  add_executable(segacd_module_test module_test.c ${module_sources} ${step_runtime_sources})
  target_include_directories(segacd_module_test PRIVATE ${step_runtime_includes} "${module_dir}")
  target_compile_features(segacd_module_test PRIVATE c_std_11)
  add_test(NAME segacd_module COMMAND segacd_module_test "${module_dir}/a.bin" "${module_dir}/b.bin")
  add_subdirectory("${engine_root}/external/m68k-recomp-core/tests/step" "core-step")

  # Deliberately opt-in private-input bring-up, outside the synthetic CTest set.
  set(SEGACD_BIOS_PROBE_MODULE_DIR "" CACHE PATH "Private generated segacd_bios module directory")
  if(SEGACD_BIOS_PROBE_MODULE_DIR)
    set(bios_sources "${SEGACD_BIOS_PROBE_MODULE_DIR}/segacd_bios_module.c")
    foreach(part RANGE 0 31)
      if(part LESS 10)
        set(part "0${part}")
      endif()
      list(APPEND bios_sources "${SEGACD_BIOS_PROBE_MODULE_DIR}/segacd_bios_part${part}.c")
    endforeach()
    add_executable(segacd_bios_probe bios_probe.c ${bios_sources} ${step_runtime_sources}
      ../../runner/cd/segacd.c ../../runner/video/genesis_bus.c ../../runner/video/genesis_vdp.c)
    target_include_directories(segacd_bios_probe PRIVATE "${SEGACD_BIOS_PROBE_MODULE_DIR}"
      ${step_runtime_includes} ../../runner/cd ../../runner/video)
    target_compile_features(segacd_bios_probe PRIVATE c_std_11)
  endif()
endif()
