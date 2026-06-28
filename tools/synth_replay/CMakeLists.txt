# synth_replay — offline "ours vs theirs" synth parity harness ([CHIP-TRACE]
# measurement tooling; DEV-ONLY — links the AGPL clownmdemu synth purely to
# observe it, like the _oracle build. Never shipped.)
cmake_minimum_required(VERSION 3.16)
project(synth_replay CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(RUNNER "${CMAKE_CURRENT_SOURCE_DIR}/../../runner")
set(CORE   "${CMAKE_CURRENT_SOURCE_DIR}/../../clownmdemu-core")

add_executable(synth_replay
    synth_replay.cpp
    # OURS
    "${RUNNER}/audio/ym2612_ymfm.cpp"
    "${RUNNER}/external/ymfm/src/ymfm_opn.cpp"
    "${RUNNER}/external/ymfm/src/ymfm_ssg.cpp"
    "${RUNNER}/external/ymfm/src/ymfm_adpcm.cpp"
    "${RUNNER}/external/ymfm/src/ymfm_pcm.cpp"
    "${RUNNER}/audio/sn76489.c"
    # THEIRS (clownmdemu synth — dev-only oracle)
    "${CORE}/source/fm.c"
    "${CORE}/source/fm-channel.c"
    "${CORE}/source/fm-operator.c"
    "${CORE}/source/fm-phase.c"
    "${CORE}/source/fm-lfo.c"
    "${CORE}/source/psg.c"
    "${CORE}/source/low-pass-filter.c"
    "${CORE}/source/log.c"
    # REFERENCE (Nuked-OPN2 die-accurate YM2612 — dev-only LGPL oracle)
    "${CMAKE_CURRENT_SOURCE_DIR}/../nuked-opn2/ym3438.c"
    # REFERENCE (BlastEm SN76489 PSG — dev-only GPLv3 oracle; PSG truth)
    "${CMAKE_CURRENT_SOURCE_DIR}/blastem_psg.c"
)

target_include_directories(synth_replay PRIVATE
    "${RUNNER}/audio"
    "${RUNNER}/include"
    "${RUNNER}/external/ymfm/src"
    "${CORE}/source"
    "${CMAKE_CURRENT_SOURCE_DIR}/../nuked-opn2"
)
