# Framework-level tests for segagenesisrecomp.
#
# Builds:
#   l1_decoder_test  â€” per-instruction decoder correctness harness.
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
add_subdirectory(segacd)
enable_language(CXX)

set(_psg_wrapper "${CMAKE_CURRENT_LIST_DIR}/../runner/audio/sn76489.c")
add_library(psg_scalar_test_model OBJECT "${_psg_wrapper}")
target_include_directories(psg_scalar_test_model PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../runner" "${CMAKE_CURRENT_LIST_DIR}/../runner/include")
target_compile_definitions(psg_scalar_test_model PRIVATE GENESIS_PSG_SILENT_SPANS=0)
foreach(_psg_api init advance write render samples_available sample_rate reset_leftover save_state load_state cosim_hash rb_save rb_load)
    target_compile_definitions(psg_scalar_test_model PRIVATE psg_${_psg_api}=ref_psg_${_psg_api})
endforeach()
add_executable(psg_span_test runtime/psg_span_test.c "${_psg_wrapper}" $<TARGET_OBJECTS:psg_scalar_test_model>)
target_include_directories(psg_span_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../runner" "${CMAKE_CURRENT_LIST_DIR}/../runner/include")
target_compile_definitions(psg_span_test PRIVATE GENESIS_PSG_SILENT_SPANS=1)
add_test(NAME psg_span COMMAND psg_span_test)

# Compile the same public wrapper under both fixed implementations. Share the
# unmodified ymfm chip, compare full state and PCM, and exercise pending output.
set(_fm_wrapper "${CMAKE_CURRENT_LIST_DIR}/../runner/audio/ym2612_ymfm.cpp")
set(_ymfm "${CMAKE_CURRENT_LIST_DIR}/../runner/external/ymfm/src")
add_library(fm_scalar_test_model OBJECT "${_fm_wrapper}")
target_include_directories(fm_scalar_test_model PRIVATE "${_ymfm}"
    "${CMAKE_CURRENT_LIST_DIR}/../runner")
target_compile_definitions(fm_scalar_test_model PRIVATE GENESIS_FM_BLOCK_SAMPLES=0)
foreach(_fm_api init advance write render samples_available sample_rate save_state load_state cosim_hash rb_save rb_load)
    target_compile_definitions(fm_scalar_test_model PRIVATE ym2612_${_fm_api}=ref_ym2612_${_fm_api})
endforeach()
add_executable(fm_block_test runtime/fm_block_test.cpp "${_fm_wrapper}"
    $<TARGET_OBJECTS:fm_scalar_test_model>
    "${_ymfm}/ymfm_opn.cpp" "${_ymfm}/ymfm_ssg.cpp"
    "${_ymfm}/ymfm_adpcm.cpp" "${_ymfm}/ymfm_pcm.cpp")
target_include_directories(fm_block_test PRIVATE "${_ymfm}"
    "${CMAKE_CURRENT_LIST_DIR}/../runner")
target_compile_definitions(fm_block_test PRIVATE GENESIS_FM_BLOCK_SAMPLES=1)
add_test(NAME fm_block COMMAND fm_block_test)

if(TARGET SDL2::SDL2)
    foreach(_host_wait_variant native portable)
        add_executable(host_wait_${_host_wait_variant}_test runtime/host_wait_test.c
            "${CMAKE_CURRENT_LIST_DIR}/../runner/host_wait.c")
        target_include_directories(host_wait_${_host_wait_variant}_test PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/../runner")
        target_link_libraries(host_wait_${_host_wait_variant}_test PRIVATE SDL2::SDL2)
        if(_host_wait_variant STREQUAL "portable")
            target_compile_definitions(host_wait_${_host_wait_variant}_test PRIVATE GENESIS_DISABLE_NATIVE_HOST_WAIT=1)
        endif()
        add_test(NAME host_wait_${_host_wait_variant} COMMAND host_wait_${_host_wait_variant}_test)
    endforeach()
    add_executable(audio_extra_test runtime/audio_extra_test.c
        "${CMAKE_CURRENT_LIST_DIR}/../runner/audio.c")
    target_include_directories(audio_extra_test PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/../runner")
    target_link_libraries(audio_extra_test PRIVATE SDL2::SDL2)
    if(UNIX)
        target_link_libraries(audio_extra_test PRIVATE m)
    endif()
    add_test(NAME audio_extra COMMAND audio_extra_test "${CMAKE_CURRENT_BINARY_DIR}/audio-extra-test.wav")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/GenesisRecompRunner.cmake")
add_executable(local_states_test runtime/local_states_test.c
    "${CMAKE_CURRENT_LIST_DIR}/../runner/local_states.c"
    "${CMAKE_CURRENT_LIST_DIR}/../runner/overlay_draw.c")
target_include_directories(local_states_test PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../runner")
target_link_libraries(local_states_test PRIVATE retcomm_rbengine)
add_test(NAME local_states COMMAND local_states_test "${CMAKE_CURRENT_BINARY_DIR}/")

add_executable(frame_pacer_test runtime/frame_pacer_test.c)
target_include_directories(frame_pacer_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../runner")
if(MSVC)
    target_compile_options(frame_pacer_test PRIVATE /UNDEBUG)
else()
    target_compile_options(frame_pacer_test PRIVATE -UNDEBUG)
    target_link_libraries(frame_pacer_test PRIVATE m)
endif()
add_test(NAME frame_pacer COMMAND frame_pacer_test)
add_executable(audio_drc_test runtime/audio_drc_test.c)
target_include_directories(audio_drc_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../runner")
if(MSVC)
    target_compile_options(audio_drc_test PRIVATE /UNDEBUG)
else()
    target_compile_options(audio_drc_test PRIVATE -UNDEBUG)
    target_link_libraries(audio_drc_test PRIVATE m)
endif()
add_test(NAME audio_drc COMMAND audio_drc_test)

find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME paired_benchmark_gate COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/runtime/test_paired_benchmark.py")
    add_test(NAME segacd_owned_extract COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/../tools/segacd/test_sgcd_owned_extract.py")
    add_test(NAME segacd_owned_media COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/../tools/segacd/test_owned_media.py")
    find_package(Git QUIET)
    if(GIT_FOUND)
        add_test(NAME build_info COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_LIST_DIR}/tools/test_build_info.py"
            --cmake "${CMAKE_COMMAND}" --git "${GIT_EXECUTABLE}")
    endif()
    add_test(NAME disasm_inline_tables COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/tools/test_disasm_inline_tables.py")
    add_test(NAME sonic_disassembly_provenance COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/tools/test_sonic_disassembly.py")
    add_test(NAME generated_input_paths COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/tools/test_generated_paths.py"
        --cmake "${CMAKE_COMMAND}" --generator "${CMAKE_GENERATOR}")
endif()

add_executable(input_script_party_test runtime/input_script_party_test.c
    "${CMAKE_CURRENT_LIST_DIR}/../runner/input_script.c")
target_include_directories(input_script_party_test PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../runner" "${CMAKE_CURRENT_LIST_DIR}/../runner/include")
add_test(NAME input_script_party COMMAND input_script_party_test "${CMAKE_CURRENT_LIST_DIR}/runtime/input_script_party.input")

add_executable(sonic1_video_test runtime/sonic1_video_test.c
    "${CMAKE_CURRENT_LIST_DIR}/../sonicthehedgehog/sonic1_video.c")
target_include_directories(sonic1_video_test PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../runner"
    "${CMAKE_CURRENT_LIST_DIR}/../runner/include"
    "${CMAKE_CURRENT_LIST_DIR}/../sonicthehedgehog")
add_test(NAME sonic1_video COMMAND sonic1_video_test)
add_executable(tile_row_test runtime/tile_row_test.c)
target_include_directories(tile_row_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../runner")
add_test(NAME tile_row COMMAND tile_row_test)
add_executable(vdp_compose_test runtime/vdp_compose_test.c)
target_include_directories(vdp_compose_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../runner")
add_test(NAME vdp_compose COMMAND vdp_compose_test)
foreach(_digest_variant fast reference)
    add_executable(rb_digest_${_digest_variant}_test runtime/rb_digest_test.c)
    target_include_directories(rb_digest_${_digest_variant}_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../runner")
    if(_digest_variant STREQUAL "reference")
        target_compile_definitions(rb_digest_${_digest_variant}_test PRIVATE GENESIS_FAST_RB_DIGEST=0)
    endif()
    add_test(NAME rb_digest_${_digest_variant} COMMAND rb_digest_${_digest_variant}_test)
endforeach()

# Snapshottable game fiber (runner/fiber_compat.c + vendored minicoro):
# save/restore/reset contract the rollback netplay core depends on.
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/GenesisRecompRunner.cmake")
add_executable(fiber_snapshot_test runtime/fiber_snapshot_test.c
    "${CMAKE_CURRENT_LIST_DIR}/../runner/fiber_compat.c")
target_include_directories(fiber_snapshot_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../runner")
genesisrecomp_runner_target(fiber_snapshot_test)
add_test(NAME fiber_snapshot COMMAND fiber_snapshot_test)

add_executable(vdp_sprite_limit_test runtime/vdp_sprite_limit_test.c
    "${CMAKE_CURRENT_LIST_DIR}/../runner/video/genesis_vdp.c")
target_include_directories(vdp_sprite_limit_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../runner/video")
add_test(NAME vdp_sprite_limit COMMAND vdp_sprite_limit_test)

add_executable(vdp_host_sprite_test runtime/vdp_host_sprite_test.c
    "${CMAKE_CURRENT_LIST_DIR}/../runner/video/genesis_vdp.c")
target_include_directories(vdp_host_sprite_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../runner/video")
add_test(NAME vdp_host_sprite COMMAND vdp_host_sprite_test)
add_executable(vdp_host_sprite_batched_test runtime/vdp_host_sprite_test.c
    "${CMAKE_CURRENT_LIST_DIR}/../runner/video/genesis_vdp.c")
target_include_directories(vdp_host_sprite_batched_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../runner/video")
target_compile_definitions(vdp_host_sprite_batched_test PRIVATE GENESIS_BATCHED_PLANES=1)
add_test(NAME vdp_host_sprite_batched COMMAND vdp_host_sprite_batched_test)
add_executable(expansion_rom_test runtime/expansion_rom_test.c)
target_include_directories(expansion_rom_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../runner/video")
add_test(NAME expansion_rom COMMAND expansion_rom_test)

if(EXISTS "${CMAKE_SOURCE_DIR}/recomp-ui/src/recomp_launcher.h")
    if(TARGET SDL2::SDL2)
        add_executable(party_input_test runtime/party_input_test.c
            "${CMAKE_CURRENT_LIST_DIR}/../runner/input_map.c"
            "${CMAKE_CURRENT_LIST_DIR}/../runner/gamepad.c"
            "${CMAKE_CURRENT_LIST_DIR}/../runner/app_config.c"
            "${CMAKE_SOURCE_DIR}/recomp-ui/src/consoles/genesis/genesis_binds.c")
        target_include_directories(party_input_test PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/../runner"
            "${CMAKE_CURRENT_LIST_DIR}/../runner/include"
            "${CMAKE_CURRENT_LIST_DIR}/../runner/external/clowncommon"
            "${CMAKE_SOURCE_DIR}/recomp-ui/src"
            "${CMAKE_SOURCE_DIR}/recomp-ui/src/common"
            "${CMAKE_SOURCE_DIR}/recomp-ui/src/platform"
            ${SDL2_PARENT_INCLUDE_DIRS})
        target_link_libraries(party_input_test PRIVATE SDL2::SDL2)
        if(WIN32)
            add_custom_command(TARGET party_input_test POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SDL2::SDL2>" "$<TARGET_FILE_DIR:party_input_test>" VERBATIM)
        endif()
        add_test(NAME party_input COMMAND party_input_test "${CMAKE_CURRENT_BINARY_DIR}/party-input-test.ini")
    endif()
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
    if(RECOMP_UI_ENABLE_MODS)
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
# m68k_validator_test â€” synthetic legality fixtures (Phase 5).
# ---------------------------------------------------------------------------
add_executable(m68k_validator_test
    harness/m68k_validator_test.c
    ${GENESISRECOMP_DECODER_SOURCES}
    "${RECOMP_SRC}/m68k_validator.c"
)
genesisrecomp_register_test(m68k_validator_test)

# ---------------------------------------------------------------------------
# m68k_decoder_synth_test â€” synthetic decoder fixtures for instruction
# forms Sonic's disasm doesn't exercise (MOVE_CCR/SR direction, CMPM,
# immediate-to-CCR/SR, ADDX/SUBX memory-predec, etc.). Grows phase-by-phase.
# ---------------------------------------------------------------------------
add_executable(m68k_decoder_synth_test
    harness/m68k_decoder_synth_test.c
    ${GENESISRECOMP_DECODER_SOURCES}
)
genesisrecomp_register_test(m68k_decoder_synth_test)

# ---------------------------------------------------------------------------
# codegen_diag_test â€” unit smoke test for the diagnostics module.
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
# return_capture_test â€” all-path proof for callees that consume JSR return PCs.
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
# m68k_effective_address_test â€” brief-extension W/L index semantics shared by
# generated code and the clean-room Tier-3 interpreter.
# ---------------------------------------------------------------------------
add_executable(m68k_effective_address_test
    harness/m68k_effective_address_test.c
)
genesisrecomp_register_test(m68k_effective_address_test)
target_include_directories(m68k_effective_address_test PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../runner/include"
)
