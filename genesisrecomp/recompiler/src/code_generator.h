/*
 * code_generator.h — 68K → C code emitter interface.
 */
#pragma once
#include <stdbool.h>
#include "rom_parser.h"
#include "function_finder.h"
#include "annotations.h"
#include "game_config.h"

bool codegen_emit(const GenesisRom *rom, const FunctionList *funcs,
                  const char *out_full_path, const char *out_dispatch_path,
                  const AnnotationTable *at, const GameConfig *cfg);
