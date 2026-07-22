include_guard(GLOBAL)
include(CMakeParseArguments)

get_filename_component(GENESISRECOMP_NET_ENGINE_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

option(GENESISRECOMP_NET_ICE
    "Enable recomp-net ICE/libjuice transport for Genesis games" OFF)

function(genesisrecomp_enable_netplay target)
    set(options ICE)
    set(one_value_args GAME_VERSION)
    cmake_parse_arguments(GEN_NET "${options}" "${one_value_args}" "" ${ARGN})

    if(NOT TARGET ${target})
        message(FATAL_ERROR
            "genesisrecomp_enable_netplay: '${target}' is not a CMake target")
    endif()

    set(_rnet_root "${GENESISRECOMP_NET_ENGINE_ROOT}/external/recomp-net")
    if(NOT EXISTS "${_rnet_root}/CMakeLists.txt")
        message(FATAL_ERROR
            "recomp-net is missing. Run git submodule update --init --recursive external/recomp-net")
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
    if(GEN_NET_GAME_VERSION)
        target_compile_definitions(${target} PRIVATE
            GENESIS_GAME_VERSION="${GEN_NET_GAME_VERSION}")
    endif()
    if(WIN32)
        target_link_libraries(${target} PRIVATE ws2_32)
    endif()

    message(STATUS
        "Genesis netplay enabled for ${target} "
        "(version=${GEN_NET_GAME_VERSION}, ICE=${RNET_ENABLE_ICE})")
endfunction()
