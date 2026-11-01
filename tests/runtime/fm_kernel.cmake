set(_fk "${CMAKE_CURRENT_LIST_DIR}")
set(_fk_runner "${_fk}/../../runner")
set(_fk_vendor "${_fk_runner}/external/ymfm/src")
set(_fk_ref "${_fk}/fm_reference")
enable_language(CXX)
add_library(fm_kernel_reference OBJECT "${_fk}/fm_kernel_model.cpp"
    "${_fk_ref}/ymfm_opn.cpp" "${_fk_ref}/ymfm_ssg.cpp"
    "${_fk_ref}/ymfm_adpcm.cpp" "${_fk_ref}/ymfm_pcm.cpp")
target_compile_features(fm_kernel_reference PRIVATE cxx_std_17)
target_include_directories(fm_kernel_reference PRIVATE "${_fk_ref}" "${_fk_runner}" "${_fk_runner}/audio")
target_compile_definitions(fm_kernel_reference PRIVATE GENESIS_FM_KERNEL_REFERENCE=1
    GENESIS_FM_BLOCK_SAMPLES=1 ymfm=baseline_ymfm)
foreach(_api init advance write render samples_available sample_rate save_state load_state cosim_hash rb_save rb_load kernel_test_status)
    target_compile_definitions(fm_kernel_reference PRIVATE ym2612_${_api}=ref_ym2612_${_api})
endforeach()
foreach(_mode IN ITEMS native fallback)
    add_executable(fm_kernel_${_mode} "${_fk}/fm_block_test.cpp" "${_fk}/fm_kernel_model.cpp"
        "${_fk_vendor}/ymfm_opn.cpp" "${_fk_vendor}/ymfm_ssg.cpp"
        "${_fk_vendor}/ymfm_adpcm.cpp" "${_fk_vendor}/ymfm_pcm.cpp"
        $<TARGET_OBJECTS:fm_kernel_reference>)
    target_compile_features(fm_kernel_${_mode} PRIVATE cxx_std_17)
    target_include_directories(fm_kernel_${_mode} PRIVATE "${_fk_vendor}" "${_fk_runner}" "${_fk_runner}/audio")
    target_compile_definitions(fm_kernel_${_mode} PRIVATE GENESIS_FM_KERNEL_TEST=1
        GENESIS_FM_KERNEL_REFERENCE=0 GENESIS_FM_BLOCK_SAMPLES=1)
    if(_mode STREQUAL "native")
        target_compile_definitions(fm_kernel_${_mode} PRIVATE YMFM_OPN2_NATIVE_KERNEL=1)
    else()
        target_compile_definitions(fm_kernel_${_mode} PRIVATE YMFM_OPN2_NATIVE_KERNEL=0)
    endif()
    add_test(NAME fm_kernel_${_mode} COMMAND fm_kernel_${_mode})
endforeach()
