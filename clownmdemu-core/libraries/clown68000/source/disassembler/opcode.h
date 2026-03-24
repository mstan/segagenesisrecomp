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

#ifndef INCLUDE_GUARD_707770EC_8F3D_4A9D_A5CD_FA706BC21B56
#define INCLUDE_GUARD_707770EC_8F3D_4A9D_A5CD_FA706BC21B56

#include "../common/instruction.h"
#include "../common/opcode.h"

typedef enum OperandAddressMode
{
	OPERAND_ADDRESS_MODE_DATA_REGISTER                                = ADDRESS_MODE_DATA_REGISTER,
	OPERAND_ADDRESS_MODE_ADDRESS_REGISTER                             = ADDRESS_MODE_ADDRESS_REGISTER,
	OPERAND_ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT                    = ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT,
	OPERAND_ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_POSTINCREMENT = ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_POSTINCREMENT,
	OPERAND_ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_PREDECREMENT  = ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_PREDECREMENT,
	OPERAND_ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_DISPLACEMENT  = ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_DISPLACEMENT,
	OPERAND_ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_INDEX         = ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_INDEX,
	OPERAND_ADDRESS_MODE_SPECIAL                                      = ADDRESS_MODE_SPECIAL,
	/* Custom - These were added for uniformity, but never actually occur in M68k opcodes. */
	OPERAND_ADDRESS_MODE_STATUS_REGISTER                              = 8,
	OPERAND_ADDRESS_MODE_CONDITION_CODE_REGISTER                      = 9,
	OPERAND_ADDRESS_MODE_NONE                                         = 10,
	OPERAND_ADDRESS_MODE_EMBEDDED_IMMEDIATE                           = 11,
	OPERAND_ADDRESS_MODE_EMBEDDED_IMMEDIATE_ADDRESS                   = 12,
	OPERAND_ADDRESS_MODE_USER_STACK_POINTER                           = 13
} OperandAddressMode;

typedef enum OperandAddressModeSpecial
{
	OPERAND_ADDRESS_MODE_REGISTER_SPECIAL_ABSOLUTE_SHORT                    = ADDRESS_MODE_REGISTER_SPECIAL_ABSOLUTE_SHORT,
	OPERAND_ADDRESS_MODE_REGISTER_SPECIAL_ABSOLUTE_LONG                     = ADDRESS_MODE_REGISTER_SPECIAL_ABSOLUTE_LONG,
	OPERAND_ADDRESS_MODE_REGISTER_SPECIAL_PROGRAM_COUNTER_WITH_DISPLACEMENT = ADDRESS_MODE_REGISTER_SPECIAL_PROGRAM_COUNTER_WITH_DISPLACEMENT,
	OPERAND_ADDRESS_MODE_REGISTER_SPECIAL_PROGRAM_COUNTER_WITH_INDEX        = ADDRESS_MODE_REGISTER_SPECIAL_PROGRAM_COUNTER_WITH_INDEX,
	OPERAND_ADDRESS_MODE_REGISTER_SPECIAL_IMMEDIATE                         = ADDRESS_MODE_REGISTER_SPECIAL_IMMEDIATE,
	/* Custom - These were added for uniformity, but never actually occur in M68k opcodes. */
	OPERAND_ADDRESS_MODE_REGISTER_SPECIAL_IMMEDIATE_ADDRESS                 = 5,
	OPERAND_ADDRESS_MODE_REGISTER_SPECIAL_REGISTER_LIST                     = 6,
	OPERAND_ADDRESS_MODE_REGISTER_SPECIAL_REGISTER_LIST_REVERSED            = 7
} OperandAddressModeSpecial;

typedef enum OperationSize
{
	OPERATION_SIZE_BYTE,
	OPERATION_SIZE_SHORT,
	OPERATION_SIZE_WORD,
	OPERATION_SIZE_LONGWORD,
	OPERATION_SIZE_NONE
} OperationSize;

typedef struct Operand
{
	OperationSize operation_size;
	OperandAddressMode address_mode;
	unsigned int address_mode_register;
} Operand;

typedef struct DecodedOpcode
{
	Instruction instruction;
	OperationSize size;
	Operand operands[2];
} DecodedOpcode;

void DecodeOpcodeAndOperands(DecodedOpcode *decoded_opcode, SplitOpcode *split_opcode, unsigned int opcode);

#endif /* INCLUDE_GUARD_707770EC_8F3D_4A9D_A5CD_FA706BC21B56 */
