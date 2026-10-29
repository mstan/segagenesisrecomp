cmake_minimum_required(VERSION 3.21)
project(GenesisZ80AdapterTests C)
enable_testing()
get_filename_component(engine "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${engine}/cmake/GenesisRecompZ80.cmake")
set(GENESIS_Z80_RECOMP ON)
set(GENESIS_Z80_AOT_SOURCE "${CMAKE_CURRENT_LIST_DIR}/adapter_dispatch_step.c")
add_executable(genesis_z80_adapter_test adapter_test.c
  "${engine}/runner/external/superzazu/z80.c"
  "${CMAKE_CURRENT_LIST_DIR}/fixture/proof_variants_step.c"
  "${engine}/tests/segacd/fixtures/z80_native/proof_control_step.c")
target_include_directories(genesis_z80_adapter_test PRIVATE "${engine}/runner"
  "${engine}/runner/include" "${engine}/runner/video"
  "${engine}/runner/external/superzazu" "${engine}/external/z80-recomp-core/include"
  "${engine}/tests/segacd/fixtures/z80_native" "${CMAKE_CURRENT_LIST_DIR}/fixture")
target_compile_features(genesis_z80_adapter_test PRIVATE c_std_11)
genesisrecomp_z80_target(genesis_z80_adapter_test)
genesisrecomp_z80_target(genesis_z80_adapter_test) # idempotent shared reconciler
foreach(mode IN ITEMS halt ei prefix irq cycles guards reset variants slice)
  add_test(NAME genesis_z80_adapter_${mode} COMMAND genesis_z80_adapter_test ${mode})
endforeach()
add_executable(genesis_z80_trace_test adapter_test.c
  "${engine}/runner/external/superzazu/z80.c"
  "${CMAKE_CURRENT_LIST_DIR}/fixture/proof_variants_step.c"
  "${engine}/tests/segacd/fixtures/z80_native/proof_control_step.c")
target_include_directories(genesis_z80_trace_test PRIVATE "${engine}/runner"
  "${engine}/runner/include" "${engine}/runner/video"
  "${engine}/runner/external/superzazu" "${engine}/external/z80-recomp-core/include"
  "${engine}/tests/segacd/fixtures/z80_native" "${CMAKE_CURRENT_LIST_DIR}/fixture")
target_compile_features(genesis_z80_trace_test PRIVATE c_std_11)
target_compile_definitions(genesis_z80_trace_test PRIVATE GEN_DEV_TRACE=1)
genesisrecomp_z80_target(genesis_z80_trace_test)
add_test(NAME genesis_z80_trace_slice COMMAND genesis_z80_trace_test slice)
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME genesis_z80_build_contract
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/build_contract_test.py" "${CMAKE_COMMAND}" "${CMAKE_GENERATOR}")
endif()
