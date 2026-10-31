set(_ew_root "${CMAKE_CURRENT_LIST_DIR}/../..")
find_package(SDL2 REQUIRED CONFIG)
foreach(_mode IN ITEMS threads fallback)
    add_executable(engine_work_${_mode}
        "${CMAKE_CURRENT_LIST_DIR}/engine_work_test.c"
        "${CMAKE_CURRENT_LIST_DIR}/pixel_original.c"
        "${_ew_root}/runner/engine_worker.c"
        "${_ew_root}/runner/frame_work.c")
    target_compile_features(engine_work_${_mode} PRIVATE c_std_11)
    target_include_directories(engine_work_${_mode} PRIVATE "${_ew_root}/runner")
    target_link_libraries(engine_work_${_mode} PRIVATE SDL2::SDL2)
    if(_mode STREQUAL "threads")
        target_compile_definitions(engine_work_${_mode} PRIVATE GENESIS_ENGINE_WORKERS=1)
    else()
        target_compile_definitions(engine_work_${_mode} PRIVATE GENESIS_ENGINE_WORKERS=0)
    endif()
    if(MSVC)
        target_compile_options(engine_work_${_mode} PRIVATE /W4 /WX)
    else()
        target_compile_options(engine_work_${_mode} PRIVATE -Wall -Wextra -Werror)
    endif()
    add_test(NAME engine_work_${_mode} COMMAND engine_work_${_mode})
endforeach()
