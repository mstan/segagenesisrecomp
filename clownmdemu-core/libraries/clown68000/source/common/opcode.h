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

#ifndef INCLUDE_GUARD_11BFE06C_ADE3_4F4B_AFB3_5854B5D48789
#define INCLUDE_GUARD_11BFE06C_ADE3_4F4B_AFB3_5854B5D48789

#include "../../libraries/clowncommon/clowncommon.h"

#include "instruction.h"

typedef enum AddressMode
{
	ADDRESS_MODE_DATA_REGISTER                                = 0,
	ADDRESS_MODE_ADDRESS_REGISTER                             = 1,
	ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT                    = 2,
	ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_POSTINCREMENT = 3,
	ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_PREDECREMENT  = 4,
	ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_DISPLACEMENT  = 5,
	ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_INDEX         = 6,
	ADDRESS_MODE_SPECIAL                                      = 7
} AddressMode;

typedef enum AddressModeSpecial
{
	ADDRESS_MODE_REGISTER_SPECIAL_ABSOLUTE_SHORT                    = 0,
	ADDRESS_MODE_REGISTER_SPECIAL_ABSOLUTE_LONG                     = 1,
	ADDRESS_MODE_REGISTER_SPECIAL_PROGRAM_COUNTER_WITH_DISPLACEMENT = 2,
	ADDRESS_MODE_REGISTER_SPECIAL_PROGRAM_COUNTER_WITH_INDEX        = 3,
	ADDRESS_MODE_REGISTER_SPECIAL_IMMEDIATE                         = 4
} AddressModeSpecial;

typedef struct SplitOpcode
{
	unsigned int raw;

	unsigned int primary_register;
	unsigned int secondary_register;
	unsigned int bits_6_and_7;

	AddressMode primary_address_mode;
	AddressMode secondary_address_mode;

	cc_bool bit_8;
} SplitOpcode;

Instruction DecodeOpcode(SplitOpcode *split_opcode, unsigned int opcode);

#endif /* INCLUDE_GUARD_11BFE06C_ADE3_4F4B_AFB3_5854B5D48789 */
