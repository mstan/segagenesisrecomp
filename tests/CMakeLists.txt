# Framework-level tests for segagenesisrecomp.
#
# Builds:
#   l1_decoder_test  — per-instruction decoder correctness harness.
#                      Reads fixture + ROM, runs m68k_decode per row,
#                      buckets failures by (level, expected, got).
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
