# runner.cmake — Source list for GenesisRecomp game projects.
#
# Usage in a game project CMakeLists.txt:
#   set(GENESISRECOMP_ROOT ${CMAKE_SOURCE_DIR}/genesisrecomp)
#   include(${GENESISRECOMP_ROOT}/runner/runner.cmake)
#   add_executable(MyGame ${GENESISRECOMP_RUNNER_SOURCES} extras.c generated/game_full.c ...)
#   target_include_directories(MyGame PRIVATE ${GENESISRECOMP_RUNNER_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR})
#   target_link_libraries(MyGame SDL2::SDL2)

set(GENESISRECOMP_RUNNER_ROOT ${CMAKE_CURRENT_LIST_DIR})

set(GENESISRECOMP_RUNNER_SOURCES
    ${GENESISRECOMP_RUNNER_ROOT}/src/main_runner.c
    ${GENESISRECOMP_RUNNER_ROOT}/src/runtime.c
    ${GENESISRECOMP_RUNNER_ROOT}/src/vdp.c
    ${GENESISRECOMP_RUNNER_ROOT}/src/ym2612.c
    ${GENESISRECOMP_RUNNER_ROOT}/src/psg.c
    ${GENESISRECOMP_RUNNER_ROOT}/src/z80.c
    ${GENESISRECOMP_RUNNER_ROOT}/src/logger.c
    ${GENESISRECOMP_RUNNER_ROOT}/src/input_script.c
    ${GENESISRECOMP_RUNNER_ROOT}/src/savestate.c
    ${GENESISRECOMP_RUNNER_ROOT}/src/launcher.c
    ${GENESISRECOMP_RUNNER_ROOT}/src/crc32.c
    ${GENESISRECOMP_RUNNER_ROOT}/src/framedump.c
)

set(GENESISRECOMP_RUNNER_INCLUDE_DIRS ${GENESISRECOMP_RUNNER_ROOT}/include)
