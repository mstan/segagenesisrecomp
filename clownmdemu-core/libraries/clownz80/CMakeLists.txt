cmake_minimum_required(VERSION 3.0...3.10)

project(clownz80 LANGUAGES C)

add_library(clownz80-common STATIC
	"source/common.c"
	"source/common.h"
)

add_library(clownz80-interpreter STATIC
	"source/interpreter.c"
	"source/interpreter.h"
)

target_link_libraries(clownz80-interpreter PRIVATE clownz80-common)

add_library(clownz80-disassembler STATIC
	"source/disassembler.c"
	"source/disassembler.h"
)

target_link_libraries(clownz80-disassembler PRIVATE clownz80-common)

add_executable(clownz80-disassembler-test
	"source/disassembler-test.c"
)

target_link_libraries(clownz80-disassembler-test PRIVATE clownz80-disassembler)
