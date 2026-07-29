cmake_minimum_required(VERSION 3.16)
project(GenesisRecomp C)

set(CMAKE_C_STANDARD 11)

set(M68K_RECOMP_CORE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../external/m68k-recomp-core")
include("${M68K_RECOMP_CORE_DIR}/cmake/M68kRecompCore.cmake")
m68k_recomp_core_sources(M68K_RECOMP_CORE_SOURCES genesis)
m68k_recomp_core_include_dirs(M68K_RECOMP_CORE_INCLUDE_DIRS genesis)

# The recompiler links no emulator core at all. Cycle stamps come from the
# clean-room MC68000 timing model in the m68k-recomp-core genesis profile
# (estimate_cycles_prm), so codegen is reproducible from the ROM and config
# alone. Set GENESIS_CYCLE_DIAG=<path> at run time for a per-instruction
# census of the emitted costs.
add_executable(GenesisRecomp
    src/main_genesis.c
    src/rom_parser.c
    ${M68K_RECOMP_CORE_SOURCES}
    src/game_config.c
    src/toml.c
)

target_include_directories(GenesisRecomp PRIVATE
    src
    ${M68K_RECOMP_CORE_INCLUDE_DIRS}
)

# Output directly to build/recompiler/
set_target_properties(GenesisRecomp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if(MSVC)
    target_compile_options(GenesisRecomp PRIVATE /W3 /WX-)
endif()
