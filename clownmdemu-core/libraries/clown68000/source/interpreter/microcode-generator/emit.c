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

#include "emit.h"

#include <stdarg.h>
#include <stdio.h>

#include "../../../libraries/clowncommon/clowncommon.h"

FILE *emit_file;
unsigned int emit_indentation;

void Emit(const char* const line)
{
	if (line[0] != '\0')
	{
		unsigned int i;

		for (i = 0; i < emit_indentation; ++i)
			fputc('\t', emit_file);

		fputs(line, emit_file);
	}

	fputc('\n', emit_file);
}

void EmitFormatted(const char* const line, ...)
{
	unsigned int i;
	va_list args;

	for (i = 0; i < emit_indentation; ++i)
		fputc('\t', emit_file);

	va_start(args, line);
	vfprintf(emit_file, line, args);
	va_end(args);

	fputc('\n', emit_file);
}
