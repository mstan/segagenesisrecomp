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

#ifndef INCLUDE_GUARD_8BAB17D5_7D64_4CA2_998B_F3F43D7FD80F
#define INCLUDE_GUARD_8BAB17D5_7D64_4CA2_998B_F3F43D7FD80F

#include <stdarg.h>

#include "../../libraries/clowncommon/clowncommon.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Clown68000_State
{
	cc_u32l data_registers[8];
	cc_u32l address_registers[8];
	cc_u32l supervisor_stack_pointer;
	cc_u32l user_stack_pointer;
	cc_u32l program_counter;
	cc_u16l status_register;
	cc_u16l instruction_register;
	cc_bool halted, stopped;
	cc_u8l pending_interrupt;
	cc_u8l leftover_cycles;
} Clown68000_State;

typedef struct Clown68000_ReadWriteCallbacks
{
	cc_u16f (*read_callback)(const void *user_data, cc_u32f address, cc_bool do_high_byte, cc_bool do_low_byte, cc_u32f current_cycle);
	void (*write_callback)(const void *user_data, cc_u32f address, cc_bool do_high_byte, cc_bool do_low_byte, cc_u32f current_cycle, cc_u16f value);
	const void *user_data;
} Clown68000_ReadWriteCallbacks;

void Clown68000_SetErrorCallback(void (*error_callback)(void *user_data, const char *format, va_list arg), const void *user_data);
void Clown68000_Reset(Clown68000_State *state, const Clown68000_ReadWriteCallbacks *callbacks);
void Clown68000_Interrupt(Clown68000_State *state, cc_u16f level);
void Clown68000_DoCycles(Clown68000_State *state, const Clown68000_ReadWriteCallbacks *callbacks, cc_u32f cycles_to_do);

#ifdef __cplusplus
}
#endif

#endif /* INCLUDE_GUARD_8BAB17D5_7D64_4CA2_998B_F3F43D7FD80F */
