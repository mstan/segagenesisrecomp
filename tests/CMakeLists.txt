# Framework-level tests for segagenesisrecomp.
#
# Builds:
#   l1_decoder_test  — per-instruction decoder correctness harness.
#                      Reads fixture + ROM, runs m68k_decode per row,
#                      buckets failures by (level, expected, got).
#
# The retired clown68000 L3 oracle and its generated manifest are intentionally
# absent. The supported cross-check is recomp-vs-clean-room-interpreter cosim
# (GENESIS_BUILD_COSIM); see COSIM.md.
#
# Depends on the recompiler sources (decoder, rom parser). Configured by
# including this directory from the parent project's CMakeLists.

cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(GenesisRecompTests C)
endif()

# This directory is consumed from game repositories rather than configured as
# a top-level project.  Keep its ROM-independent harnesses runnable directly
# with `ctest --test-dir <game-build>/tests -C <config>`.
enable_testing()

add_executable(sonic1_video_test runtime/sonic1_video_test.c
    "${CMAKE_CURRENT_LIST_DIR}/../sonicthehedgehog/sonic1_video.c")
target_include_directories(sonic1_video_test PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../runner"
    "${CMAKE_CURRENT_LIST_DIR}/../runner/include"
    "${CMAKE_CURRENT_LIST_DIR}/../sonicthehedgehog")
add_test(NAME sonic1_video COMMAND sonic1_video_test)

add_executable(sonic2_video_test runtime/sonic2_video_test.c)
target_include_directories(sonic2_video_test PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../runner"
    "${CMAKE_CURRENT_LIST_DIR}/../runner/include")
add_test(NAME sonic2_video COMMAND sonic2_video_test)

if(EXISTS "${CMAKE_SOURCE_DIR}/recomp-ui/src/recomp_launcher.h")
    add_executable(video_mod_test runtime/video_mod_test.c "${CMAKE_CURRENT_LIST_DIR}/../runner/app_config.c")
    # Exercise the real launcher model with the consumer's actual compile gate:
    # a working provider alone does not guarantee the Mods entry is visible.
    target_sources(video_mod_test PRIVATE
        "${CMAKE_SOURCE_DIR}/recomp-ui/src/common/launcher_model.c"
        "${CMAKE_SOURCE_DIR}/recomp-ui/src/common/crc32.c"
        "${CMAKE_SOURCE_DIR}/recomp-ui/src/common/sha256.c"
        "${CMAKE_SOURCE_DIR}/recomp-ui/src/common/sha1.c"
        "${CMAKE_SOURCE_DIR}/recomp-ui/src/common/ips_patch.c"
        "${CMAKE_SOURCE_DIR}/recomp-ui/src/consoles/psx/memcard_format.c")
    target_compile_definitions(video_mod_test PRIVATE RECOMP_LAUNCHER=1
        RECOMP_UI_ENABLE_MODS=$<BOOL:${RECOMP_UI_ENABLE_MODS}>)
    if(TARGET SonicTheHedgehogRecomp OR TARGET SonicTheHedgehog2Recomp)
        target_compile_definitions(video_mod_test PRIVATE EXPECT_CUSTOM_VIDEO_MODS=1)
    endif()
    target_include_directories(video_mod_test PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/../runner"
        "${CMAKE_CURRENT_LIST_DIR}/../runner/external/clowncommon"
        "${CMAKE_SOURCE_DIR}/recomp-ui/src/common"
        "${CMAKE_SOURCE_DIR}/recomp-ui/src")
    add_test(NAME video_mod COMMAND video_mod_test "${CMAKE_CURRENT_BINARY_DIR}/video-mod-test.ini")
endif()

set(RECOMP_SRC "${CMAKE_CURRENT_LIST_DIR}/../recompiler/src")
add_executable(main_cpu_clock_test runtime/main_cpu_clock_test.c)
if(MSVC)
    target_compile_options(main_cpu_clock_test PRIVATE /UNDEBUG)
else()
    target_compile_options(main_cpu_clock_test PRIVATE -UNDEBUG)
endif()
add_test(NAME main_cpu_clock COMMAND main_cpu_clock_test)

function(genesisrecomp_configure_test TARGET)
    target_include_directories(${TARGET} PRIVATE "${RECOMP_SRC}")
    if(MSVC)
        target_compile_definitions(${TARGET} PRIVATE _CRT_SECURE_NO_WARNINGS)
        target_compile_options(${TARGET} PRIVATE /W3 /wd4996)
    endif()
endfunction()

function(genesisrecomp_register_test TARGET)
    genesisrecomp_configure_test(${TARGET})
    string(REGEX REPLACE "_test$" "" TEST_NAME "${TARGET}")
    add_test(NAME ${TEST_NAME} COMMAND ${TARGET})
endfunction()

set(GENESISRECOMP_DECODER_SOURCES
    "${RECOMP_SRC}/m68k_decoder.c"
    "${RECOMP_SRC}/rom_parser.c"
)

add_executable(rom_parser_test
    harness/rom_parser_test.c
    "${RECOMP_SRC}/rom_parser.c"
)
genesisrecomp_register_test(rom_parser_test)

add_executable(l1_decoder_test
    harness/l1_decoder_test.c
    ${GENESISRECOMP_DECODER_SOURCES}
)
genesisrecomp_configure_test(l1_decoder_test)

# ---------------------------------------------------------------------------
# m68k_validator_test — synthetic legality fixtures (Phase 5).
# ---------------------------------------------------------------------------
add_executable(m68k_validator_test
    harness/m68k_validator_test.c
    ${GENESISRECOMP_DECODER_SOURCES}
    "${RECOMP_SRC}/m68k_validator.c"
)
genesisrecomp_register_test(m68k_validator_test)

# ---------------------------------------------------------------------------
# m68k_decoder_synth_test — synthetic decoder fixtures for instruction
# forms Sonic's disasm doesn't exercise (MOVE_CCR/SR direction, CMPM,
# immediate-to-CCR/SR, ADDX/SUBX memory-predec, etc.). Grows phase-by-phase.
# ---------------------------------------------------------------------------
add_executable(m68k_decoder_synth_test
    harness/m68k_decoder_synth_test.c
    ${GENESISRECOMP_DECODER_SOURCES}
)
genesisrecomp_register_test(m68k_decoder_synth_test)

# ---------------------------------------------------------------------------
# codegen_diag_test — unit smoke test for the diagnostics module.
# Exercises codegen_diag directly without spinning up a ROM/codegen
# pipeline; ensures recording, counting, and summary stay correct as
# new diag kinds are added in later phases.
# ---------------------------------------------------------------------------
add_executable(codegen_diag_test
    harness/codegen_diag_test.c
    ${GENESISRECOMP_DECODER_SOURCES}
    "${RECOMP_SRC}/codegen_diag.c"
)
genesisrecomp_register_test(codegen_diag_test)

# ---------------------------------------------------------------------------
# return_capture_test — all-path proof for callees that consume JSR return PCs.
# ---------------------------------------------------------------------------
add_executable(return_capture_test
    harness/return_capture_test.c
    ${GENESISRECOMP_DECODER_SOURCES}
    "${CMAKE_CURRENT_LIST_DIR}/../external/m68k-recomp-core/profiles/genesis/return_capture.c"
)
genesisrecomp_register_test(return_capture_test)
target_include_directories(return_capture_test PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../external/m68k-recomp-core/profiles/genesis"
)

# ---------------------------------------------------------------------------
# m68k_effective_address_test — brief-extension W/L index semantics shared by
# generated code and the clean-room Tier-3 interpreter.
# ---------------------------------------------------------------------------
add_executable(m68k_effective_address_test
    harness/m68k_effective_address_test.c
)
genesisrecomp_register_test(m68k_effective_address_test)
target_include_directories(m68k_effective_address_test PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../runner/include"
)
