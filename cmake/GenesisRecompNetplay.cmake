include_guard(GLOBAL)
include(CMakeParseArguments)

get_filename_component(GENESISRECOMP_NET_ENGINE_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

option(GENESISRECOMP_NET_ICE
    "Enable recomp-net ICE/libjuice transport for Genesis games" OFF)

# Netplay is OPT-IN. A game calling genesisrecomp_enable_netplay() declares that
# it *supports* netplay; it does not force every build to carry it. Building it
# pulls in the recomp-net submodule, a full network stack, and (with ICE) a
# libjuice FetchContent — none of which a dev doing single-player work needs.
#
# It used to be mandatory: the helper hard-errored when external/recomp-net was
# absent, so a clone without that submodule could not configure Sonic 2 at all.
# That is exactly how it failed for a user building on macOS.
#
# Turn it on with -DGENESISRECOMP_NETPLAY=ON (the submodule must be checked out;
# `git submodule update --init --recursive external/recomp-net`). Release builds
# that are supposed to ship netplay MUST pass it explicitly.
option(GENESISRECOMP_NETPLAY
    "Build netplay support (requires the recomp-net submodule)" OFF)

function(genesisrecomp_enable_netplay target)
    set(options ICE PEER_VIEW)
    set(one_value_args GAME_VERSION)
    cmake_parse_arguments(GEN_NET "${options}" "${one_value_args}" "" ${ARGN})

    if(NOT TARGET ${target})
        message(FATAL_ERROR
            "genesisrecomp_enable_netplay: '${target}' is not a CMake target")
    endif()

    if(NOT GENESISRECOMP_NETPLAY)
        # The runner already compiles fine without it: every call site is behind
        # #if GENESIS_HAS_RECOMP_NET, which stays undefined here.
        message(STATUS
            "Genesis netplay: OFF for ${target} "
            "(opt in with -DGENESISRECOMP_NETPLAY=ON)")
        return()
    endif()

    set(_rnet_root "${GENESISRECOMP_NET_ENGINE_ROOT}/external/recomp-net")
    if(NOT EXISTS "${_rnet_root}/CMakeLists.txt")
        message(FATAL_ERROR
            "GENESISRECOMP_NETPLAY=ON but the recomp-net submodule is not checked "
            "out at:\n    ${_rnet_root}\n\n"
            "Fetch it with:\n"
            "    git submodule update --init --recursive external/recomp-net\n\n"
            "Or configure with -DGENESISRECOMP_NETPLAY=OFF (the default) to build "
            "without netplay.")
    endif()

    if(GEN_NET_ICE OR GENESISRECOMP_NET_ICE)
        set(RNET_ENABLE_ICE ON CACHE BOOL "" FORCE)
    endif()
    set(RNET_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(RNET_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    if(NOT TARGET recomp_net)
        add_subdirectory("${_rnet_root}"
                         "${CMAKE_BINARY_DIR}/recomp-net" EXCLUDE_FROM_ALL)
    endif()
    # rbengine: the tick-keyed snapshot ring the rollback host stores
    # runner/rb_state.c blobs in (external/rbengine, RetroPortingToolKit).
    set(_rbe_root "${GENESISRECOMP_NET_ENGINE_ROOT}/external/rbengine")
    if(NOT EXISTS "${_rbe_root}/CMakeLists.txt")
        message(FATAL_ERROR
            "GENESISRECOMP_NETPLAY=ON but the rbengine submodule is not checked out at:\n"
            "    ${_rbe_root}\n"
            "Fetch it with: git submodule update --init external/rbengine")
    endif()
    set(RBE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    if(NOT TARGET retcomm_rbengine)
        add_subdirectory("${_rbe_root}" "${CMAKE_BINARY_DIR}/rbengine" EXCLUDE_FROM_ALL)
    endif()

    set(_np "${GENESISRECOMP_NET_ENGINE_ROOT}/runner/netplay")
    target_sources(${target} PRIVATE
        "${_np}/genesis_netplay.c"
        "${_np}/genesis_netplay_rb.c"
        "${_np}/genesis_netplay_identity.c")
    target_include_directories(${target} PRIVATE "${_np}")
    target_link_libraries(${target} PRIVATE recomp_net retcomm_rbengine)
    target_compile_definitions(${target} PRIVATE GENESIS_HAS_RECOMP_NET=1)
    if(RNET_ENABLE_ICE)
        target_compile_definitions(${target} PRIVATE RNET_ENABLE_ICE=1)
    endif()
    # The lobby: recomp-ui's shared netplay backend (recomp_netplay_host),
    # bound by runner/netplay/genesis_host_lobby.c. Needs the launcher.
    if(COMMAND recomp_target_launcher_netplay)
        recomp_target_launcher_netplay(${target})
        target_sources(${target} PRIVATE "${_np}/genesis_host_lobby.c")
        target_compile_definitions(${target} PRIVATE GENESIS_HAS_HOST_LOBBY=1)
    else()
        message(FATAL_ERROR
            "genesisrecomp_enable_netplay(${target}): recomp-ui's "
            "recomp_target_launcher_netplay() is not available. Include "
            "recomp_ui.cmake (a recomp-ui with the shared netplay backend, "
            "RetroPortingToolKit/recomp-ui b688ca7 or later) before enabling "
            "netplay.")
    endif()
    if(GEN_NET_PEER_VIEW)
        target_compile_definitions(${target} PRIVATE
            GENESIS_NETPLAY_PEER_VIEW=1)
    endif()
    if(GEN_NET_GAME_VERSION)
        target_compile_definitions(${target} PRIVATE
            GENESIS_GAME_VERSION="${GEN_NET_GAME_VERSION}")
    endif()
    if(WIN32)
        target_link_libraries(${target} PRIVATE ws2_32)
    endif()
    # recomp-net's netsim (RNET_SIM_*) is compiled in; nothing to add.

    message(STATUS
        "Genesis netplay enabled for ${target} "
        "(version=${GEN_NET_GAME_VERSION}, ICE=${RNET_ENABLE_ICE}, "
        "peer_view=${GEN_NET_PEER_VIEW})")
endfunction()
