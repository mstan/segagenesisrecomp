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

    target_sources(${target} PRIVATE
        "${GENESISRECOMP_NET_ENGINE_ROOT}/runner/netplay/genesis_netplay.c"
        "${GENESISRECOMP_NET_ENGINE_ROOT}/runner/netplay/genesis_launcher_netplay.c"
        "${GENESISRECOMP_NET_ENGINE_ROOT}/runner/lobby/genesis_lobby_client.c"
        "${GENESISRECOMP_NET_ENGINE_ROOT}/runner/lobby/ws/rnet_ws.c"
        "${GENESISRECOMP_NET_ENGINE_ROOT}/runner/lobby/ws/rnet_sha1.c")
    target_include_directories(${target} PRIVATE
        "${GENESISRECOMP_NET_ENGINE_ROOT}/runner/netplay"
        "${GENESISRECOMP_NET_ENGINE_ROOT}/runner/lobby"
        "${GENESISRECOMP_NET_ENGINE_ROOT}/runner/lobby/ws")
    target_link_libraries(${target} PRIVATE recomp_net)
    target_compile_definitions(${target} PRIVATE
        GENESIS_HAS_RECOMP_NET=1
        GENESIS_HAS_LOBBY_CLIENT=1)
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

    message(STATUS
        "Genesis netplay enabled for ${target} "
        "(version=${GEN_NET_GAME_VERSION}, ICE=${RNET_ENABLE_ICE}, "
        "peer_view=${GEN_NET_PEER_VIEW})")
endfunction()
