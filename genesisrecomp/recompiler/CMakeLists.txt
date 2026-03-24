cmake_minimum_required(VERSION 3.16)
project(GenesisRecomp C)

set(CMAKE_C_STANDARD 11)

# Pure C11, zero external dependencies
add_executable(GenesisRecomp
    src/main_genesis.c
    src/rom_parser.c
    src/m68k_decoder.c
    src/function_finder.c
    src/code_generator.c
    src/annotations.c
    src/game_config.c
)

target_include_directories(GenesisRecomp PRIVATE src)

# Output directly to build/recompiler/
set_target_properties(GenesisRecomp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if(MSVC)
    target_compile_options(GenesisRecomp PRIVATE /W3 /WX-)
endif()
