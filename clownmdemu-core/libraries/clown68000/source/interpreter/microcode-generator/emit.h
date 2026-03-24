/*
    clown68000 - A Motorola 68000 emulator.
    Copyright (C) 2021-2023  Clownacy

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as published
    by the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef INCLUDE_GUARD_A3BEF769_9DE7_478E_BF00_AF24854A823C
#define INCLUDE_GUARD_A3BEF769_9DE7_478E_BF00_AF24854A823C

#include <stdio.h>

#include "../../../libraries/clowncommon/clowncommon.h"

extern FILE *emit_file;
extern unsigned int emit_indentation;

void Emit(const char* const line);
CC_ATTRIBUTE_PRINTF(1, 2) void EmitFormatted(const char* const line, ...);

#endif /* INCLUDE_GUARD_A3BEF769_9DE7_478E_BF00_AF24854A823C */
