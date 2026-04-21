cmake_minimum_required(VERSION 3.16)
project(GenesisRecomp C)

set(CMAKE_C_STANDARD 11)

# We link clown68000 into the recompiler so cycle_probe can single-step each
# instruction at codegen time and emit the interpreter's own cycle count. The
# existing submodule patch (runner/clown68000_patch.patch) has already been
# applied to the source; we disable its coverage logging here so codegen
# traffic doesn't pollute the coverage table.
set(C68K_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../clownmdemu-core/libraries/clown68000/source")

add_executable(GenesisRecomp
    src/main_genesis.c
    src/rom_parser.c
    src/m68k_decoder.c
    src/function_finder.c
    src/code_generator.c
    src/annotations.c
    src/game_config.c
    src/cycle_probe.c
    "${C68K_DIR}/interpreter/clown68000.c"
    "${C68K_DIR}/common/opcode.c"
)

target_compile_definitions(GenesisRecomp PRIVATE
    CLOWN68000_COVERAGE_LOG_DISABLE
)

target_include_directories(GenesisRecomp PRIVATE
    src
    "${C68K_DIR}/interpreter"
    "${C68K_DIR}/common"
    "${CMAKE_CURRENT_SOURCE_DIR}/../clownmdemu-core/libraries/clowncommon"
)

# Output directly to build/recompiler/
set_target_properties(GenesisRecomp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if(MSVC)
    target_compile_options(GenesisRecomp PRIVATE /W3 /WX-)
endif()
