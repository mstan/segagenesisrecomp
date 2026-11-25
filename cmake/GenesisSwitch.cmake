# Loaded by CMAKE_PROJECT_INCLUDE: use each game's normal source/generation
# rules, with the shared Switch platform applied after its targets exist.
if(NOT NINTENDO_SWITCH OR NOT PROJECT_IS_TOP_LEVEL)
    return()
endif()
include_guard(GLOBAL)
if(EXISTS "${GENESIS_HOST_STEP_RECOMPILER}" AND NOT TARGET M68kStepRecomp)
    add_executable(M68kStepRecomp IMPORTED GLOBAL)
    set_target_properties(M68kStepRecomp PROPERTIES
        IMPORTED_LOCATION "${GENESIS_HOST_STEP_RECOMPILER}")
endif()
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)
set(CMAKE_DL_LIBS "")
find_package(SDL2 CONFIG REQUIRED)
if(NOT TARGET SDL2::SDL2main)
    add_library(SDL2::SDL2main INTERFACE IMPORTED)
endif()
find_package(OpenGL CONFIG REQUIRED)
set(CMAKE_THREAD_LIBS_INIT "-lpthread")
set(CMAKE_HAVE_LIBC_PTHREAD 1)
set(Threads_FOUND TRUE)
set(GENESIS_SWITCH_PLATFORM "${CMAKE_CURRENT_LIST_DIR}/../runner/platform/switch")

function(_genesis_switch_targets)
    get_property(_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_target IN LISTS _targets)
        get_target_property(_type ${_target} TYPE)
        if(NOT _type STREQUAL "EXECUTABLE")
            continue()
        endif()
        get_target_property(_sources ${_target} SOURCES)
        if(NOT "${_sources}" MATCHES "/runner/main.c")
            continue()
        endif()
        # Use recomp-ui's existing in-app SD browser. Desktop dialog helpers
        # spawn external programs, which do not exist on Horizon.
        list(FILTER _sources EXCLUDE REGEX "/(launcher_files.c|tinyfiledialogs.c)$")
        set_property(TARGET ${_target} PROPERTY SOURCES "${_sources}")
        target_sources(${_target} PRIVATE
            "${GENESIS_SWITCH_PLATFORM}/platform.c"
            "${GENESIS_SWITCH_PLATFORM}/storage.c"
            "${GENESIS_SWITCH_PLATFORM}/launcher_files.c")
        target_link_libraries(${_target} PRIVATE glad EGL glapi drm_nouveau nx pthread)
        target_link_options(${_target} PRIVATE "-Wl,--wrap=SDL_GL_CreateContext"
            "-Wl,--wrap=rename" "-Wl,--wrap=fopen")
        target_compile_definitions(${_target} PRIVATE IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS)
        # ImGui's desktop loader uses dlopen. Switch obtains GL through SDL.
        set_property(SOURCE
            "${CMAKE_SOURCE_DIR}/recomp-ui/src/third_party/imgui/backends/imgui_impl_opengl3.cpp"
            APPEND PROPERTY COMPILE_OPTIONS "-DIMGUI_IMPL_OPENGL_LOADER_CUSTOM;-include;glad/glad.h")
        foreach(_gl_source common/launcher_gl.c common/launcher_debug.c
                common/backends/imgui/launcher_imgui.cpp)
            set_property(SOURCE "${CMAKE_SOURCE_DIR}/recomp-ui/src/${_gl_source}"
                APPEND PROPERTY COMPILE_OPTIONS "-include;glad/glad.h")
        endforeach()
        nx_generate_nacp(OUTPUT "${_target}.nacp" NAME "${GENESIS_SWITCH_NAME}"
            AUTHOR "Genesis Recomp" VERSION "${PROJECT_VERSION}")
        nx_create_nro(${_target} NACP "${_target}.nacp")
    endforeach()
endfunction()
cmake_language(DEFER CALL _genesis_switch_targets)
