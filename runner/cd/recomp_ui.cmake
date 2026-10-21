include(FetchContent)

set(_SEGACD_RECOMP_UI_HELPER_DIR "${CMAKE_CURRENT_LIST_DIR}")

set(SEGACD_RECOMP_UI_ROOT "" CACHE PATH
    "Path to a local recomp-ui checkout; empty fetches the pinned runtime source")

if(SEGACD_RECOMP_UI_ROOT)
    if(NOT EXISTS "${SEGACD_RECOMP_UI_ROOT}/src/recomp_runtime_ui.h")
        message(FATAL_ERROR
            "SEGACD_RECOMP_UI_ROOT does not contain src/recomp_runtime_ui.h: "
            "${SEGACD_RECOMP_UI_ROOT}")
    endif()
    set(_segacd_recomp_ui_root "${SEGACD_RECOMP_UI_ROOT}")
else()
    FetchContent_Declare(segacd_recomp_ui_source
        GIT_REPOSITORY https://github.com/mstan/recomp-ui.git
        GIT_TAG b9ef2f539be3622eeb0e7d4a2ff33c8edf129667
        GIT_SHALLOW FALSE)
    FetchContent_GetProperties(segacd_recomp_ui_source)
    if(NOT segacd_recomp_ui_source_POPULATED)
        FetchContent_Populate(segacd_recomp_ui_source)
    endif()
    set(_segacd_recomp_ui_root "${segacd_recomp_ui_source_SOURCE_DIR}")
endif()

if(NOT TARGET segacd_recomp_runtime_ui)
    add_library(segacd_recomp_runtime_ui STATIC
        "${_segacd_recomp_ui_root}/src/common/recomp_runtime_ui.c"
        "${_segacd_recomp_ui_root}/src/common/recomp_runtime_settings.c")
    target_include_directories(segacd_recomp_runtime_ui PUBLIC
        "${_segacd_recomp_ui_root}/src")
    target_include_directories(segacd_recomp_runtime_ui PRIVATE
        "${_segacd_recomp_ui_root}/src/common")
    target_compile_features(segacd_recomp_runtime_ui PUBLIC c_std_11)
endif()

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/licenses")
configure_file("${_segacd_recomp_ui_root}/LICENSE"
               "${CMAKE_CURRENT_BINARY_DIR}/licenses/recomp-ui-LICENSE.txt"
               COPYONLY)

function(segacd_target_recomp_ui target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "segacd_target_recomp_ui: unknown target '${target}'")
    endif()
    target_sources("${target}" PRIVATE
        "${_SEGACD_RECOMP_UI_HELPER_DIR}/presentation.c"
        "${_SEGACD_RECOMP_UI_HELPER_DIR}/presentation_ui.c")
    target_include_directories("${target}" PRIVATE
        "${_SEGACD_RECOMP_UI_HELPER_DIR}"
        "${_segacd_recomp_ui_root}/src")
    target_link_libraries("${target}" PRIVATE segacd_recomp_runtime_ui)
    add_custom_command(TARGET "${target}" POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "$<TARGET_FILE_DIR:${target}>/licenses"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${_segacd_recomp_ui_root}/LICENSE"
            "$<TARGET_FILE_DIR:${target}>/licenses/recomp-ui.txt"
        VERBATIM)
endfunction()
