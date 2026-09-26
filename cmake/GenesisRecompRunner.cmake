# Shared Genesis runner: source list and target requirements.
#
# This file is the engine's single statement of which runner translation
# units a game executable must compile, and which build settings the runner
# needs from the executable that links it.
#
#   genesisrecomp_runner_sources(<out-var>
#       [TRACE <bool>]            # cmd_server + frame_snapshots, else the stub
#       [REVERSE_DEBUG <bool>])   # reverse_debug.c
#   genesisrecomp_runner_target(<target>)
#       # applies the runner's link/compile requirements to <target>
#
# GENESIS_RUNNER_CORE_SOURCES lists the unconditional core (paths relative to
# runner/). A game adopting this module replaces its hand-maintained list with
#   genesisrecomp_runner_sources(RUNNER_SOURCES TRACE ${GEN_ENABLE_TRACE} ...)
# and calls genesisrecomp_runner_target() on every runner executable.
#
# Unmigrated consumers.  Game repositories that still list runner sources by
# hand keep building when the engine adds a core translation unit: every
# consumer includes GenesisRecompGenerated.cmake, which includes this file,
# and a deferred reconciler then visits each executable in that directory
# that compiles runner/glue.c (i.e. links the runner), appends any core
# source it does not already list, and applies genesisrecomp_runner_target().
# Reconciliation is idempotent, adds nothing to targets already using this
# module, and prints one STATUS line per target it had to complete so the
# game repository can migrate.

include_guard(GLOBAL)

get_filename_component(GENESIS_RUNNER_ENGINE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(GENESIS_RUNNER_CORE_SOURCES
    main.c
    sim_step.c
    rb_state.c
    rb_probe.c
    audio.c
    glue.c
    fiber_compat.c
    crash_report.c
    input_script.c
    gamepad.c
    input_map.c
    app_config.c
    chip_trace.c
    cosim_state.c
    audio/event_queue.c
    audio/ym2612_ymfm.cpp
    external/ymfm/src/ymfm_opn.cpp
    external/ymfm/src/ymfm_ssg.cpp
    external/ymfm/src/ymfm_adpcm.cpp
    external/ymfm/src/ymfm_pcm.cpp
    audio/sn76489.c
    audio/mixer.c
    audio/observability.c
    audio/audio_shadow.c
    audio/fm_shadow.cpp
    video/color_lut.c
    video/genesis_vdp.c
    video/genesis_bus.c
    video/genesis_machine.c
    external/superzazu/z80.c
    m68k_interp.c)

# Core sources an unmigrated consumer may be missing and that the reconciler
# is allowed to add. Everything else in the core list predates this module
# and is listed by every consumer; a consumer lacking one of those is a
# deliberate divergence the reconciler must not paper over.
set(GENESIS_RUNNER_RECONCILED_SOURCES
    cosim_state.c
    sim_step.c
    rb_state.c
    rb_probe.c)

function(genesisrecomp_runner_sources out_var)
    cmake_parse_arguments(GRS "" "TRACE;REVERSE_DEBUG" "" ${ARGN})
    if(NOT DEFINED GRS_TRACE)
        set(GRS_TRACE ON)
    endif()
    set(_root "${GENESIS_RUNNER_ENGINE_ROOT}/runner")
    set(_list "")
    foreach(_s IN LISTS GENESIS_RUNNER_CORE_SOURCES)
        list(APPEND _list "${_root}/${_s}")
    endforeach()
    if(GRS_TRACE)
        list(APPEND _list "${_root}/cmd_server.c" "${_root}/frame_snapshots.c")
    else()
        list(APPEND _list "${_root}/cmd_server_stub.c")
    endif()
    if(GRS_REVERSE_DEBUG)
        list(APPEND _list "${_root}/reverse_debug.c")
    endif()
    list(APPEND _list
        "${GENESIS_RUNNER_ENGINE_ROOT}/recompiler/src/m68k_decoder.c"
        "${GENESIS_RUNNER_ENGINE_ROOT}/recompiler/src/rom_parser.c")
    set(${out_var} "${_list}" PARENT_SCOPE)
endfunction()

# Build requirements the runner places on the executable that links it.
function(genesisrecomp_runner_target target)
    get_target_property(_done ${target} GENESIS_RUNNER_REQUIREMENTS)
    if(_done)
        return()
    endif()
    set_property(TARGET ${target} PROPERTY GENESIS_RUNNER_REQUIREMENTS ON)
    # The game fiber's context switch (runner/fiber_compat.c, minicoro)
    # replaces the stack pointer without a shadow-stack switch, so the image
    # must not opt in to user-mode hardware shadow stacks (Intel CET).
    # fiber_convert_thread() also verifies the policy at startup.
    if(MSVC)
        target_link_options(${target} PRIVATE "/CETCOMPAT:NO")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
           CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$" AND
           CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        # An object without the SHSTK property keeps the whole ELF unmarked,
        # so glibc never enables shadow stacks for the process.
        target_compile_options(${target} PRIVATE
            "$<$<COMPILE_LANGUAGE:C,CXX>:-fcf-protection=none>")
    endif()
    # Page-by-page stack probes for large frames on the game fiber, so a frame
    # bigger than the fiber guard region (FIBER_GUARD_BYTES, fiber_compat.h)
    # faults inside the guard instead of jumping over it into the coroutine
    # header. MSVC's __chkstk already does this. The define lets
    # tests/runtime/fiber_snapshot_test.c assert it (--overflow-huge).
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND NOT MSVC)
        include(CheckCCompilerFlag)
        check_c_compiler_flag(-fstack-clash-protection GENESIS_HAVE_STACK_CLASH)
        if(GENESIS_HAVE_STACK_CLASH)
            target_compile_options(${target} PRIVATE
                "$<$<COMPILE_LANGUAGE:C,CXX>:-fstack-clash-protection>")
            target_compile_definitions(${target} PRIVATE GENESIS_STACK_CLASH_PROTECTION=1)
        endif()
    endif()
endfunction()

function(_genesisrecomp_norm out path base)
    if(NOT IS_ABSOLUTE "${path}")
        set(path "${base}/${path}")
    endif()
    get_filename_component(_real "${path}" REALPATH)
    string(TOLOWER "${_real}" _real)
    set(${out} "${_real}" PARENT_SCOPE)
endfunction()

function(_genesisrecomp_reconcile_runner_targets)
    if(DEFINED RUNNER_ROOT)
        set(_root "${RUNNER_ROOT}")
    else()
        set(_root "${GENESIS_RUNNER_ENGINE_ROOT}/runner")
    endif()
    _genesisrecomp_norm(_glue "${_root}/glue.c" "${CMAKE_CURRENT_SOURCE_DIR}")
    get_property(_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_t IN LISTS _targets)
        get_target_property(_type ${_t} TYPE)
        if(NOT _type STREQUAL "EXECUTABLE")
            continue()
        endif()
        get_target_property(_srcs ${_t} SOURCES)
        get_target_property(_sdir ${_t} SOURCE_DIR)
        set(_have "")
        foreach(_s IN LISTS _srcs)
            if(_s MATCHES "^\\$<")
                continue()
            endif()
            _genesisrecomp_norm(_n "${_s}" "${_sdir}")
            list(APPEND _have "${_n}")
        endforeach()
        if(NOT _glue IN_LIST _have)
            continue()
        endif()
        set(_added "")
        foreach(_rel IN LISTS GENESIS_RUNNER_RECONCILED_SOURCES)
            _genesisrecomp_norm(_n "${_root}/${_rel}" "${_sdir}")
            if(NOT _n IN_LIST _have)
                target_sources(${_t} PRIVATE "${_root}/${_rel}")
                list(APPEND _added "${_rel}")
            endif()
        endforeach()
        genesisrecomp_runner_target(${_t})
        if(_added)
            string(REPLACE ";" ", " _added_s "${_added}")
            message(STATUS "genesis runner: ${_t} did not list ${_added_s}; added "
                           "(migrate to cmake/GenesisRecompRunner.cmake)")
        endif()
    endforeach()
endfunction()

# One deferred reconciliation per including directory.
get_property(_genesis_runner_deferred DIRECTORY PROPERTY GENESIS_RUNNER_RECONCILE_DEFERRED)
if(NOT _genesis_runner_deferred)
    set_property(DIRECTORY PROPERTY GENESIS_RUNNER_RECONCILE_DEFERRED ON)
    cmake_language(DEFER CALL _genesisrecomp_reconcile_runner_targets)
endif()
unset(_genesis_runner_deferred)
