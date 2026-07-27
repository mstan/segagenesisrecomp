# synth_replay — offline "ours vs theirs" synth parity harness ([CHIP-TRACE]
# measurement tooling; DEV-ONLY — links the AGPL clownmdemu synth purely to
# observe it, like the _oracle build. Never shipped.)
cmake_minimum_required(VERSION 3.16)
project(synth_replay CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(RUNNER "${CMAKE_CURRENT_SOURCE_DIR}/../../runner")
set(CORE   "${CMAKE_CURRENT_SOURCE_DIR}/../../clownmdemu-core")

# This harness exists to compare our synth against clownmdemu's, so it cannot
# be built without it. clownmdemu-core is an OPT-IN submodule (AGPL, private
# fork) — fail with the fix rather than a wall of missing-source errors.
if(NOT EXISTS "${CORE}/source/fm.c")
    message(FATAL_ERROR
        "synth_replay needs the optional clownmdemu-core submodule, which is "
        "not checked out at:\n    ${CORE}\n\n"
        "Fetch it with:\n"
        "    git submodule update --init --checkout --recursive clownmdemu-core\n\n"
        "Nothing that ships requires it; see cmake/GenesisOracle.cmake.")
endif()

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
)

target_include_directories(synth_replay PRIVATE
    "${RUNNER}/audio"
    "${RUNNER}/include"
    "${RUNNER}/external/ymfm/src"
    "${CORE}/source"
)
