# launcher.cmake — RmlUi pre-boot GUI launcher integration for Genesis targets.
#
# The shared runner already builds the bare-bones path (no launcher = today's
# behaviour). This file adds the optional graphical launcher (launcher_gui.cpp +
# RmlUi + the SDL/GL3 backend), modeled on nesrecomp/runner/launcher.cmake.
#
# Usage from a game project's CMakeLists.txt, AFTER add_executable(<target> ...)
# and AFTER SDL2 is linked on <target>:
#   include(${RUNNER_ROOT}/launcher/launcher.cmake)
#   genesisrecomp_add_launcher(<target>)
#
# Wire it onto the NATIVE (shipped) target only — never the dev-only _oracle
# target. Pulls in OpenGL + the vendored RmlUi / FreeType static libs
# (runner/launcher/deps, all permissive licenses) and defines GENESIS_LAUNCHER=1
# so main.c compiles the genesis_launcher_run_window() gate.

set(GENESIS_LAUNCHER_ROOT ${CMAKE_CURRENT_LIST_DIR})

function(genesisrecomp_add_launcher TARGET)
    find_package(OpenGL REQUIRED)

    # Build the vendored RmlUi (rmlui_core) + FreeType (freetype) static libs once
    # per build tree. Guarded so multiple targets / re-includes don't double-add.
    if(NOT TARGET rmlui_core)
        add_subdirectory("${GENESIS_LAUNCHER_ROOT}/deps" "${CMAKE_BINARY_DIR}/launcher-deps")
    endif()

    set(_rml "${GENESIS_LAUNCHER_ROOT}/deps/RmlUi")
    target_sources(${TARGET} PRIVATE
        ${GENESIS_LAUNCHER_ROOT}/launcher_gui.cpp
        ${GENESIS_LAUNCHER_ROOT}/stb_image_impl.cpp
        ${_rml}/Backends/RmlUi_Renderer_GL3.cpp
        ${_rml}/Backends/RmlUi_Platform_SDL.cpp
    )
    target_include_directories(${TARGET} PRIVATE
        ${_rml}/Include
        ${_rml}/Backends
        ${GENESIS_LAUNCHER_ROOT}
        ${GENESIS_LAUNCHER_ROOT}/third_party
    )
    target_link_libraries(${TARGET} PRIVATE rmlui_core freetype OpenGL::GL)
    target_compile_definitions(${TARGET} PRIVATE
        GENESIS_LAUNCHER=1
        RMLUI_STATIC_LIB
        RMLUI_NO_THIRDPARTY_CONTAINERS
    )

    # Stage the launcher assets (launcher.rml + fonts + img) next to the exe so the
    # GUI can load them at runtime, under launcher/.
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${GENESIS_LAUNCHER_ROOT}/assets"
            "$<TARGET_FILE_DIR:${TARGET}>/launcher"
        COMMENT "Staging launcher assets -> launcher/")

    # Per-game box art: drop boxart.png (single-game repo) or boxart-<short>.png
    # (multi-target repo — the launcher picks img/boxart-<short_name>.png, falling
    # back to boxart.png). Every boxart*.png in the repo root is staged.
    file(GLOB _boxarts "${CMAKE_SOURCE_DIR}/boxart*.png")
    foreach(_bx ${_boxarts})
        get_filename_component(_bxname "${_bx}" NAME)
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_bx}" "$<TARGET_FILE_DIR:${TARGET}>/launcher/img/${_bxname}"
            COMMENT "Staging ${_bxname}")
    endforeach()
endfunction()
