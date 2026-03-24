cmake_minimum_required(VERSION 3.0...3.12)

option(CC_USE_C99_INTEGERS "Use C99 integer types instead of the original C89 ones. May save RAM depending on the platform's data model." OFF)

project(clown68000 LANGUAGES C)

# Common

add_library(clown68000-common STATIC
	"libraries/clowncommon/clowncommon.h"
	"source/common/instruction.h"
	"source/common/opcode.c"
	"source/common/opcode.h"	
)

if(CC_USE_C99_INTEGERS)
	target_compile_definitions(clown68000-common PUBLIC CC_USE_C99_INTEGERS)
endif()

# Interpreter

add_library(clown68000-interpreter STATIC
	"source/interpreter/clown68000.c"
	"source/interpreter/clown68000.h"
)

target_link_libraries(clown68000-interpreter PRIVATE clown68000-common) 

if(CC_USE_C99_INTEGERS)
	target_compile_definitions(clown68000-interpreter PUBLIC CC_USE_C99_INTEGERS)
endif()

# Microcode Generator

add_executable(clown68000-microcode-generator
	"source/interpreter/microcode-generator/emit.c"
	"source/interpreter/microcode-generator/emit.h"
	"source/interpreter/microcode-generator/emit-instructions.c"
	"source/interpreter/microcode-generator/emit-instructions.h"
	"source/interpreter/microcode-generator/instruction-properties.c"
	"source/interpreter/microcode-generator/instruction-properties.h"
	"source/interpreter/microcode-generator/generator.c"
)

if(CC_USE_C99_INTEGERS)
	target_compile_definitions(clown68000-microcode-generator PUBLIC CC_USE_C99_INTEGERS)
endif()

# Disassembler

add_library(clown68000-disassembler STATIC
	"source/disassembler/disassembler.c"
	"source/disassembler/disassembler.h"
	"source/disassembler/opcode.c"
	"source/disassembler/opcode.h"
)

target_link_libraries(clown68000-disassembler PRIVATE clown68000-common) 

if(CC_USE_C99_INTEGERS)
	target_compile_definitions(clown68000-disassembler PUBLIC CC_USE_C99_INTEGERS)
endif()

# Disassembler Tool

add_executable(clown68000-disassembler-tool
	"source/disassembler/main.c"
)

target_link_libraries(clown68000-disassembler-tool PRIVATE clown68000-disassembler) 

if(CC_USE_C99_INTEGERS)
	target_compile_definitions(clown68000-disassembler-tool PUBLIC CC_USE_C99_INTEGERS)
endif()
