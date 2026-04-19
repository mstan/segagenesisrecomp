# Framework-level tests for segagenesisrecomp.
#
# Builds:
#   l1_decoder_test  — per-instruction decoder correctness harness.
#                      Reads fixture + ROM, runs m68k_decode per row,
#                      buckets failures by (level, expected, got).
#
#   l3_oracle_test   — per-function semantic correctness harness.
#                      Runs each translated function via clown68000 and
#                      via the recompiled C from the same initial state,
#                      diffs registers + RAM. Catches codegen bugs the
#                      structural L2 invariants can't see.
#
# Depends on the recompiler sources (decoder, rom parser). Configured by
# including this directory from the parent project's CMakeLists.

cmake_minimum_required(VERSION 3.16)

set(RECOMP_SRC "${CMAKE_CURRENT_LIST_DIR}/../recompiler/src")

add_executable(l1_decoder_test
    harness/l1_decoder_test.c
    "${RECOMP_SRC}/m68k_decoder.c"
    "${RECOMP_SRC}/rom_parser.c"
)
target_include_directories(l1_decoder_test PRIVATE "${RECOMP_SRC}")
if(MSVC)
    target_compile_definitions(l1_decoder_test PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(l1_decoder_test PRIVATE /W3 /wd4996)
endif()

# ---------------------------------------------------------------------------
# L3 oracle. Links against the same generated sonic_full.c +
# sonic_dispatch.c that ship in the game binary, plus clown68000 as the
# semantic ground truth. NO SDL, NO clownmdemu-core, NO runner — the
# harness provides sandbox-backed stubs for every runtime symbol the
# generated code references.
# ---------------------------------------------------------------------------

set(SONIC_GEN_DIR "${CMAKE_CURRENT_LIST_DIR}/../sonicthehedgehog/generated")
set(RUNNER_INC   "${CMAKE_CURRENT_LIST_DIR}/../runner/include")
set(CLOWN68_DIR  "${CMAKE_CURRENT_LIST_DIR}/../clownmdemu-core/libraries/clown68000")

add_executable(l3_oracle_test
    harness/l3_oracle_test.c
    "${SONIC_GEN_DIR}/sonic_full.c"
    "${SONIC_GEN_DIR}/sonic_dispatch.c"
)
target_include_directories(l3_oracle_test PRIVATE
    "${RUNNER_INC}"
    "${CLOWN68_DIR}/source/interpreter"
    "${CLOWN68_DIR}/libraries/clowncommon"
)
# clown68000-interpreter + clown68000-common are built by the outer
# project's add_subdirectory(clownmdemu-core, ...). We rebuild this
# target as part of the same configure so the static-lib targets are
# already available.
target_link_libraries(l3_oracle_test PRIVATE
    clown68000-interpreter
    clown68000-common
)
if(MSVC)
    target_compile_definitions(l3_oracle_test PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(l3_oracle_test PRIVATE /W3 /wd4996 /wd4244 /wd4267)
endif()
