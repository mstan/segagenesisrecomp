set(_aw_root "${CMAKE_CURRENT_LIST_DIR}/../..")
set(_aw_runner "${_aw_root}/runner")
set(_aw_ymfm "${_aw_runner}/external/ymfm/src")
enable_language(CXX)
find_package(SDL2 REQUIRED CONFIG)
add_library(audio_work_original OBJECT
    "${CMAKE_CURRENT_LIST_DIR}/work_reference/mixer_wrapper.c"
    "${_aw_runner}/audio/event_queue.c"
    "${_aw_runner}/audio/ym2612_ymfm.cpp"
    "${_aw_runner}/audio/sn76489.c")
foreach(_name IN ITEMS audio_mixer_init audio_mixer_drain
    audio_event_state_size audio_event_rb_save audio_event_rb_load audio_event_state_save audio_event_state_load
    audio_event_cosim_stream_hash audio_event_cosim_hist_total audio_event_cosim_hist
    audio_event_push audio_event_pop audio_event_queue_reset audio_event_requeue audio_event_queue_count audio_event_cosim_hash)
    target_compile_definitions(audio_work_original PRIVATE ${_name}=ref_${_name})
endforeach()
foreach(_prefix IN ITEMS ym2612 psg)
    foreach(_name IN ITEMS init advance write render samples_available sample_rate reset_leftover save_state load_state cosim_hash rb_save rb_load)
        target_compile_definitions(audio_work_original PRIVATE ${_prefix}_${_name}=ref_${_prefix}_${_name})
    endforeach()
endforeach()
target_include_directories(audio_work_original PRIVATE "${_aw_runner}" "${_aw_runner}/audio" "${_aw_ymfm}")
target_compile_definitions(audio_work_original PRIVATE GENESIS_FM_BLOCK_SAMPLES=1 GENESIS_PSG_SILENT_SPANS=1)
add_executable(audio_work_test "${CMAKE_CURRENT_LIST_DIR}/audio_work_test.cpp"
    "${_aw_runner}/audio/mixer.c" "${_aw_runner}/audio/event_queue.c"
    "${_aw_runner}/audio/ym2612_ymfm.cpp" "${_aw_runner}/audio/sn76489.c"
    "${_aw_runner}/engine_worker.c" $<TARGET_OBJECTS:audio_work_original>
    "${_aw_ymfm}/ymfm_opn.cpp" "${_aw_ymfm}/ymfm_ssg.cpp"
    "${_aw_ymfm}/ymfm_adpcm.cpp" "${_aw_ymfm}/ymfm_pcm.cpp")
target_compile_features(audio_work_test PRIVATE c_std_11 cxx_std_17)
target_include_directories(audio_work_test PRIVATE "${_aw_runner}" "${_aw_runner}/audio" "${_aw_ymfm}")
target_compile_definitions(audio_work_test PRIVATE GENESIS_ENGINE_WORKERS=1 GENESIS_FM_BLOCK_SAMPLES=1 GENESIS_PSG_SILENT_SPANS=1)
target_link_libraries(audio_work_test PRIVATE SDL2::SDL2)
if(MSVC)
    target_compile_definitions(audio_work_test PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_definitions(audio_work_original PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME audio_work COMMAND audio_work_test "${CMAKE_CURRENT_BINARY_DIR}/audio-original-applied.log" "${CMAKE_CURRENT_BINARY_DIR}/audio-worker-applied.log")
