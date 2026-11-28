# Optional board-independent audio components. No Genesis runner, guest CPU,
# presentation or board mapping is introduced by this target.
set_property(GLOBAL PROPERTY GENESIS_AUDIO_COMPONENT_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")

function(genesis_add_audio_components target)
    get_property(_GENESIS_AUDIO_COMPONENT_ROOT GLOBAL PROPERTY GENESIS_AUDIO_COMPONENT_ROOT)
    add_library(${target} STATIC
        "${_GENESIS_AUDIO_COMPONENT_ROOT}/runner/audio/event_queue.c"
        "${_GENESIS_AUDIO_COMPONENT_ROOT}/runner/audio/ym2612_ymfm.cpp"
        "${_GENESIS_AUDIO_COMPONENT_ROOT}/runner/external/ymfm/src/ymfm_opn.cpp"
        "${_GENESIS_AUDIO_COMPONENT_ROOT}/runner/external/ymfm/src/ymfm_ssg.cpp"
        "${_GENESIS_AUDIO_COMPONENT_ROOT}/runner/external/ymfm/src/ymfm_adpcm.cpp")
    target_include_directories(${target} PUBLIC
        "${_GENESIS_AUDIO_COMPONENT_ROOT}/runner/audio"
        PRIVATE "${_GENESIS_AUDIO_COMPONENT_ROOT}/runner"
                "${_GENESIS_AUDIO_COMPONENT_ROOT}/runner/external/ymfm/src")
    target_compile_features(${target} PUBLIC c_std_11 PRIVATE cxx_std_17)
endfunction()
