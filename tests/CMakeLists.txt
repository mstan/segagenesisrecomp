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

add_executable(trilogy_campaign_test runtime/trilogy_campaign_test.c
    "${CMAKE_CURRENT_LIST_DIR}/../sonic3k/trilogy_campaign.c")
add_executable(trilogy_sram_test runtime/trilogy_sram_test.c
    "${CMAKE_CURRENT_LIST_DIR}/../sonic3k/trilogy_sram.c")
add_executable(trilogy_progress_test runtime/trilogy_progress_test.c
    "${CMAKE_CURRENT_LIST_DIR}/../sonic3k/trilogy_campaign.c"
    "${CMAKE_CURRENT_LIST_DIR}/../sonic3k/trilogy_sram.c"
    "${CMAKE_CURRENT_LIST_DIR}/../sonic3k/trilogy_progress.c")
add_executable(trilogy_playtest_seed "${CMAKE_CURRENT_LIST_DIR}/../tools/trilogy_playtest_seed.c"
    "${CMAKE_CURRENT_LIST_DIR}/../sonic3k/trilogy_campaign.c"
    "${CMAKE_CURRENT_LIST_DIR}/../sonic3k/trilogy_sram.c"
    "${CMAKE_CURRENT_LIST_DIR}/../sonic3k/trilogy_progress.c")
add_executable(trilogy_cleared_save "${CMAKE_CURRENT_LIST_DIR}/../tools/trilogy_cleared_save.c"
    "${CMAKE_CURRENT_LIST_DIR}/../sonic3k/trilogy_campaign.c"
    "${CMAKE_CURRENT_LIST_DIR}/../sonic3k/trilogy_sram.c"
    "${CMAKE_CURRENT_LIST_DIR}/../sonic3k/trilogy_progress.c")
add_executable(trilogy_music_test runtime/trilogy_music_test.c
    "${CMAKE_CURRENT_LIST_DIR}/../sonic3k/trilogy_music.c"
    "${CMAKE_CURRENT_LIST_DIR}/../sonic3k/trilogy_codecs.c")
target_compile_options(trilogy_music_test PRIVATE $<$<C_COMPILER_ID:MSVC>:/UNDEBUG>)
target_compile_options(trilogy_music_test PRIVATE $<$<NOT:$<C_COMPILER_ID:MSVC>>:-UNDEBUG>)
add_executable(trilogy_assets_test runtime/trilogy_assets_test.c
    "${CMAKE_CURRENT_LIST_DIR}/../sonic3k/trilogy_assets.c"
    "${CMAKE_CURRENT_LIST_DIR}/../sonic3k/trilogy_object_assets.c"
    "${CMAKE_CURRENT_LIST_DIR}/../sonic3k/trilogy_codecs.c")
target_compile_definitions(trilogy_assets_test PRIVATE TRILOGY_ASSETS_TRACE=1)
if(MSVC)
    target_compile_options(trilogy_campaign_test PRIVATE /UNDEBUG)
    target_compile_options(trilogy_sram_test PRIVATE /UNDEBUG)
    target_compile_options(trilogy_progress_test PRIVATE /UNDEBUG)
    target_compile_options(trilogy_assets_test PRIVATE /UNDEBUG)
else()
    target_compile_options(trilogy_campaign_test PRIVATE -UNDEBUG)
    target_compile_options(trilogy_sram_test PRIVATE -UNDEBUG)
    target_compile_options(trilogy_progress_test PRIVATE -UNDEBUG)
    target_compile_options(trilogy_assets_test PRIVATE -UNDEBUG)
endif()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/trilogy-fixtures")
add_test(NAME trilogy_campaign COMMAND trilogy_campaign_test)
set_tests_properties(trilogy_campaign PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/trilogy-fixtures")
add_test(NAME trilogy_sram COMMAND trilogy_sram_test)
add_test(NAME trilogy_progress COMMAND trilogy_progress_test)
add_test(NAME trilogy_codecs COMMAND trilogy_assets_test)
set_tests_properties(trilogy_sram PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/trilogy-fixtures")
set_tests_properties(trilogy_progress PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/trilogy-fixtures")

add_executable(sonic2_campaign_test runtime/sonic2_campaign_test.c
    "${CMAKE_CURRENT_LIST_DIR}/../sonicthehedgehog2/sonic2_campaign.c"
    "${CMAKE_CURRENT_LIST_DIR}/../sonicthehedgehog2/sonic2_campaign_store.c")
if(MSVC)
    target_compile_options(sonic2_campaign_test PRIVATE /UNDEBUG)
else()
    target_compile_options(sonic2_campaign_test PRIVATE -UNDEBUG)
endif()
add_test(NAME sonic2_campaign COMMAND sonic2_campaign_test)

add_executable(sonic2_campaign_file_test runtime/sonic2_campaign_file_test.c
    "${CMAKE_CURRENT_LIST_DIR}/../sonicthehedgehog2/sonic2_campaign.c"
    "${CMAKE_CURRENT_LIST_DIR}/../sonicthehedgehog2/sonic2_campaign_store.c"
    "${CMAKE_CURRENT_LIST_DIR}/../sonicthehedgehog2/sonic2_campaign_file.c"
    "${CMAKE_CURRENT_LIST_DIR}/../sonicthehedgehog2/sonic2_party.c")
if(MSVC)
    target_compile_options(sonic2_campaign_file_test PRIVATE /UNDEBUG)
else()
    target_compile_options(sonic2_campaign_file_test PRIVATE -UNDEBUG)
endif()
add_test(NAME sonic2_campaign_file COMMAND sonic2_campaign_file_test)

add_executable(sonic2_party_test runtime/sonic2_party_test.c
    "${CMAKE_CURRENT_LIST_DIR}/../sonicthehedgehog2/sonic2_party.c")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/party-fixtures")
add_test(NAME sonic2_party COMMAND sonic2_party_test)
set_tests_properties(sonic2_party PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/party-fixtures")

add_executable(input_script_party_test runtime/input_script_party_test.c
    "${CMAKE_CURRENT_LIST_DIR}/../runner/input_script.c")
target_include_directories(input_script_party_test PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../runner" "${CMAKE_CURRENT_LIST_DIR}/../runner/include")
add_test(NAME input_script_party COMMAND input_script_party_test "${CMAKE_CURRENT_LIST_DIR}/runtime/input_script_party.input")

add_executable(sonic2_donor_test runtime/sonic2_donor_test.c
    "${CMAKE_CURRENT_LIST_DIR}/../sonicthehedgehog2/sonic2_donor_assets.c")
add_test(NAME sonic2_donor COMMAND sonic2_donor_test)

add_executable(sonic1_video_test runtime/sonic1_video_test.c
    "${CMAKE_CURRENT_LIST_DIR}/../sonicthehedgehog/sonic1_video.c")
target_include_directories(sonic1_video_test PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../runner"
    "${CMAKE_CURRENT_LIST_DIR}/../runner/include"
    "${CMAKE_CURRENT_LIST_DIR}/../sonicthehedgehog")
add_test(NAME sonic1_video COMMAND sonic1_video_test)

add_executable(vdp_sprite_limit_test runtime/vdp_sprite_limit_test.c
    "${CMAKE_CURRENT_LIST_DIR}/../runner/video/genesis_vdp.c")
target_include_directories(vdp_sprite_limit_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../runner/video")
add_test(NAME vdp_sprite_limit COMMAND vdp_sprite_limit_test)

add_executable(sonic2_video_test runtime/sonic2_video_test.c)
target_include_directories(sonic2_video_test PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../runner"
    "${CMAKE_CURRENT_LIST_DIR}/../runner/include")
add_test(NAME sonic2_video COMMAND sonic2_video_test)

add_executable(sonic3_video_test runtime/sonic3_video_test.c)
target_include_directories(sonic3_video_test PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../runner"
    "${CMAKE_CURRENT_LIST_DIR}/../runner/include")
add_test(NAME sonic3_video COMMAND sonic3_video_test)

add_executable(sonic3_standalone_video_test runtime/sonic3_video_test.c)
target_compile_definitions(sonic3_standalone_video_test PRIVATE SONIC3_STANDALONE=1)
target_include_directories(sonic3_standalone_video_test PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../runner"
    "${CMAKE_CURRENT_LIST_DIR}/../runner/include")
add_test(NAME sonic3_standalone_video COMMAND sonic3_standalone_video_test)

# Blue Spheres' projection uses the C math library. Unlike the Windows
# CRT, Unix toolchains require this dependency explicitly at link time.
if(UNIX)
    target_link_libraries(sonic3_video_test PRIVATE m)
    target_link_libraries(sonic3_standalone_video_test PRIVATE m)
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/recomp-ui/src/recomp_launcher.h")
    add_executable(sonic2_save_preview "${CMAKE_CURRENT_LIST_DIR}/../tools/sonic2_save_preview.c"
        "${CMAKE_CURRENT_LIST_DIR}/../sonicthehedgehog2/sonic2_campaign.c"
        "${CMAKE_CURRENT_LIST_DIR}/../sonicthehedgehog2/sonic2_save_assets.c"
        "${CMAKE_CURRENT_LIST_DIR}/../sonicthehedgehog2/sonic2_save_draw.c"
        "${CMAKE_SOURCE_DIR}/recomp-ui/src/common/sha256.c")
    target_include_directories(sonic2_save_preview PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/../sonicthehedgehog2"
        "${CMAKE_CURRENT_LIST_DIR}/../runner"
        "${CMAKE_SOURCE_DIR}/recomp-ui/src/common")
    add_executable(sonic2_donor_inspect "${CMAKE_CURRENT_LIST_DIR}/../tools/sonic2_donor_inspect.c"
        "${CMAKE_CURRENT_LIST_DIR}/../sonicthehedgehog2/sonic2_donor_assets.c"
        "${CMAKE_SOURCE_DIR}/recomp-ui/src/common/sha256.c")
    target_include_directories(sonic2_donor_inspect PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/../sonicthehedgehog2"
        "${CMAKE_CURRENT_LIST_DIR}/../runner"
        "${CMAKE_CURRENT_LIST_DIR}/../runner/video"
        "${CMAKE_SOURCE_DIR}/recomp-ui/src/common")
    if(TARGET SDL2::SDL2 AND TARGET SonicTheHedgehog2Recomp)
        add_executable(sonic2_character_test runtime/sonic2_character_test.c
            "${CMAKE_CURRENT_LIST_DIR}/../sonicthehedgehog2/sonic2_character.c")
        target_include_directories(sonic2_character_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../sonicthehedgehog2")
        if(MSVC)
            target_compile_options(sonic2_character_test PRIVATE /UNDEBUG)
        else()
            target_compile_options(sonic2_character_test PRIVATE -UNDEBUG)
        endif()
        add_test(NAME sonic2_character COMMAND sonic2_character_test)
        add_executable(sonic2_resources_test runtime/sonic2_resources_test.c
            "${CMAKE_CURRENT_LIST_DIR}/../sonicthehedgehog2/sonic2_party.c"
            "${CMAKE_CURRENT_LIST_DIR}/../sonicthehedgehog2/sonic2_campaign.c"
            "${CMAKE_CURRENT_LIST_DIR}/../sonicthehedgehog2/sonic2_campaign_store.c"
            "${CMAKE_CURRENT_LIST_DIR}/../sonicthehedgehog2/sonic2_campaign_file.c"
            "${CMAKE_CURRENT_LIST_DIR}/../sonicthehedgehog2/sonic2_resources.c"
            "${CMAKE_CURRENT_LIST_DIR}/../sonicthehedgehog2/sonic2_save_assets.c"
            "${CMAKE_CURRENT_LIST_DIR}/../sonicthehedgehog2/sonic2_donor_assets.c"
            "${CMAKE_CURRENT_LIST_DIR}/../sonicthehedgehog2/sonic2_mods.c"
            "${CMAKE_SOURCE_DIR}/recomp-ui/src/common/sha256.c")
        target_compile_definitions(sonic2_resources_test PRIVATE RECOMP_LAUNCHER=1)
        target_include_directories(sonic2_resources_test PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/../sonicthehedgehog2"
            "${CMAKE_SOURCE_DIR}/recomp-ui/src" "${CMAKE_SOURCE_DIR}/recomp-ui/src/common")
        if(MSVC)
            target_compile_options(sonic2_resources_test PRIVATE /UNDEBUG)
        else()
            target_compile_options(sonic2_resources_test PRIVATE -UNDEBUG)
        endif()
        add_test(NAME sonic2_resources COMMAND sonic2_resources_test "${CMAKE_CURRENT_BINARY_DIR}/resource-test-absent.ini")
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
                "${SDL2_BUNDLED}/lib/x64/SDL2.dll" "$<TARGET_FILE_DIR:party_input_test>")
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
    if(TARGET SonicTheHedgehogRecomp OR TARGET SonicTheHedgehog2Recomp OR TARGET Sonic3KRecomp)
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
