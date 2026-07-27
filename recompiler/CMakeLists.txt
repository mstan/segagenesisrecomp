cmake_minimum_required(VERSION 3.16)
project(GenesisRecomp C)

set(CMAKE_C_STANDARD 11)

set(M68K_RECOMP_CORE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../external/m68k-recomp-core")
include("${M68K_RECOMP_CORE_DIR}/cmake/M68kRecompCore.cmake")
m68k_recomp_core_sources(M68K_RECOMP_CORE_SOURCES genesis)
m68k_recomp_core_include_dirs(M68K_RECOMP_CORE_INCLUDE_DIRS genesis)

# The recompiler emits cycle stamps from the clean-room PRM model in
# code_generator.c and links NO emulator core. clownmdemu-core is optional
# and dev-only: with -DGENESIS_CYCLE_ORACLE=ON we additionally link
# clown68000 so the GENESIS_CYCLE_DIAG census can print clown's measurement
# beside the model's for validation. It is never a cost source, so codegen
# output is identical with the option on or off.
set(CLOWN_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../clownmdemu-core")
option(GENESIS_CYCLE_ORACLE
       "Link clown68000 as a validation oracle for the cycle model (AGPL, dev-only)"
       OFF)

if(GENESIS_CYCLE_ORACLE AND NOT EXISTS "${CLOWN_CORE_DIR}/libraries/clown68000/source/interpreter/clown68000.c")
    message(FATAL_ERROR
        "GENESIS_CYCLE_ORACLE=ON but the optional clownmdemu-core submodule is "
        "not checked out.\n"
        "Fetch it with:  git submodule update --init --checkout --recursive clownmdemu-core\n"
        "(That submodule is opt-in; see README.md \"Cloning\".)")
endif()

add_executable(GenesisRecomp
    src/main_genesis.c
    src/rom_parser.c
    ${M68K_RECOMP_CORE_SOURCES}
    src/game_config.c
    src/cycle_probe.c
    src/toml.c
)

target_include_directories(GenesisRecomp PRIVATE
    src
    ${M68K_RECOMP_CORE_INCLUDE_DIRS}
)

if(GENESIS_CYCLE_ORACLE)
    set(C68K_DIR "${CLOWN_CORE_DIR}/libraries/clown68000/source")
    target_sources(GenesisRecomp PRIVATE
        "${C68K_DIR}/interpreter/clown68000.c"
        "${C68K_DIR}/common/opcode.c"
    )
    target_include_directories(GenesisRecomp PRIVATE
        "${C68K_DIR}/interpreter"
        "${C68K_DIR}/common"
        "${CLOWN_CORE_DIR}/libraries/clowncommon"
    )
    # Our clown68000 changes are committed in the mstan fork directly (no
    # patch step); disable its coverage logging so codegen traffic doesn't
    # pollute the coverage table.
    target_compile_definitions(GenesisRecomp PRIVATE
        GENESIS_CYCLE_ORACLE
        CLOWN68000_COVERAGE_LOG_DISABLE
    )
    message(STATUS "GenesisRecomp: cycle oracle ON (clown68000 linked, validation only)")
endif()

# Output directly to build/recompiler/
set_target_properties(GenesisRecomp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if(MSVC)
    target_compile_options(GenesisRecomp PRIVATE /W3 /WX-)
endif()
