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

#include "opcode.h"

static OperationSize GetSize(const Instruction instruction, const SplitOpcode* const opcode)
{
	OperationSize operation_size;

	operation_size = OPERATION_SIZE_NONE;

	switch (instruction)
	{
		case INSTRUCTION_ORI_TO_CCR:
		case INSTRUCTION_ANDI_TO_CCR:
		case INSTRUCTION_EORI_TO_CCR:
		case INSTRUCTION_NBCD:
		case INSTRUCTION_TAS:
		case INSTRUCTION_SCC:
		case INSTRUCTION_SBCD:
		case INSTRUCTION_ABCD:
			/* Hardcoded to a byte. */
			operation_size = OPERATION_SIZE_BYTE;
			break;

		case INSTRUCTION_ORI_TO_SR:
		case INSTRUCTION_ANDI_TO_SR:
		case INSTRUCTION_EORI_TO_SR:
		case INSTRUCTION_MOVE_FROM_SR:
		case INSTRUCTION_MOVE_TO_SR:
		case INSTRUCTION_MOVE_TO_CCR:
		case INSTRUCTION_LINK:
		case INSTRUCTION_CHK:
		case INSTRUCTION_DBCC:
		case INSTRUCTION_ASD_MEMORY:
		case INSTRUCTION_LSD_MEMORY:
		case INSTRUCTION_ROXD_MEMORY:
		case INSTRUCTION_ROD_MEMORY:
		case INSTRUCTION_STOP:
		case INSTRUCTION_BRA_WORD:
		case INSTRUCTION_BSR_WORD:
		case INSTRUCTION_BCC_WORD:
		case INSTRUCTION_DIVU:
		case INSTRUCTION_DIVS:
		case INSTRUCTION_MULU:
		case INSTRUCTION_MULS:
			/* Hardcoded to a word. */
			operation_size = OPERATION_SIZE_WORD;
			break;

		case INSTRUCTION_ADDAQ:
		case INSTRUCTION_SUBAQ:
		case INSTRUCTION_SWAP:
		case INSTRUCTION_LEA:
		case INSTRUCTION_MOVEQ:
		case INSTRUCTION_MOVE_USP:
			/* Hardcoded to a longword. */
			operation_size = OPERATION_SIZE_LONGWORD;
			break;

		case INSTRUCTION_SUBA:
		case INSTRUCTION_CMPA:
		case INSTRUCTION_ADDA:
			/* Word or longword based on bit 8. */
			operation_size = opcode->bit_8 ? OPERATION_SIZE_LONGWORD : OPERATION_SIZE_WORD;
			break;

		case INSTRUCTION_BTST_STATIC:
		case INSTRUCTION_BCHG_STATIC:
		case INSTRUCTION_BCLR_STATIC:
		case INSTRUCTION_BSET_STATIC:
		case INSTRUCTION_BTST_DYNAMIC:
		case INSTRUCTION_BCHG_DYNAMIC:
		case INSTRUCTION_BCLR_DYNAMIC:
		case INSTRUCTION_BSET_DYNAMIC:
			/* 4 if register - 1 if memory. */
			operation_size = opcode->primary_address_mode == ADDRESS_MODE_DATA_REGISTER ? OPERATION_SIZE_LONGWORD : OPERATION_SIZE_BYTE;
			break;

		case INSTRUCTION_MOVEA:
		case INSTRUCTION_MOVE:
			/* Derived from an odd bitfield. */
			switch (opcode->raw & 0x3000)
			{
				case 0x1000:
					operation_size = OPERATION_SIZE_BYTE;
					break;
			
				case 0x2000:
					operation_size = OPERATION_SIZE_LONGWORD; /* Yup, this isn't a typo. */
					break;
			
				case 0x3000:
					operation_size = OPERATION_SIZE_WORD;
					break;
			}

			break;

		case INSTRUCTION_EXT:
			operation_size = (opcode->raw & 0x0040) != 0 ? OPERATION_SIZE_LONGWORD : OPERATION_SIZE_WORD;
			break;

		case INSTRUCTION_ORI:
		case INSTRUCTION_ANDI:
		case INSTRUCTION_SUBI:
		case INSTRUCTION_ADDI:
		case INSTRUCTION_EORI:
		case INSTRUCTION_CMPI:
		case INSTRUCTION_NEGX:
		case INSTRUCTION_CLR:
		case INSTRUCTION_NEG:
		case INSTRUCTION_NOT:
		case INSTRUCTION_TST:
		case INSTRUCTION_OR:
		case INSTRUCTION_SUB:
		case INSTRUCTION_SUBX:
		case INSTRUCTION_EOR:
		case INSTRUCTION_CMPM:
		case INSTRUCTION_CMP:
		case INSTRUCTION_AND:
		case INSTRUCTION_ADD:
		case INSTRUCTION_ADDQ:
		case INSTRUCTION_SUBQ:
		case INSTRUCTION_ADDX:
		case INSTRUCTION_ASD_REGISTER:
		case INSTRUCTION_LSD_REGISTER:
		case INSTRUCTION_ROXD_REGISTER:
		case INSTRUCTION_ROD_REGISTER:
			/* Standard. */
			switch (1 << opcode->bits_6_and_7)
			{
				case 1:
					operation_size = OPERATION_SIZE_BYTE;
					break;

				case 2:
					operation_size = OPERATION_SIZE_WORD;
					break;

				case 4:
					operation_size = OPERATION_SIZE_LONGWORD;
					break;
			}

			break;

		case INSTRUCTION_BRA_SHORT:
		case INSTRUCTION_BSR_SHORT:
		case INSTRUCTION_BCC_SHORT:
			operation_size = OPERATION_SIZE_SHORT;
			break;

		case INSTRUCTION_MOVEP:
		case INSTRUCTION_MOVEM:
			operation_size = (opcode->bits_6_and_7 & 1) != 0 ? OPERATION_SIZE_LONGWORD : OPERATION_SIZE_WORD;
			break;

		case INSTRUCTION_PEA:
		case INSTRUCTION_ILLEGAL:
		case INSTRUCTION_TRAP:
		case INSTRUCTION_UNLK:
		case INSTRUCTION_RESET:
		case INSTRUCTION_NOP:
		case INSTRUCTION_RTE:
		case INSTRUCTION_RTS:
		case INSTRUCTION_TRAPV:
		case INSTRUCTION_RTR:
		case INSTRUCTION_JSR:
		case INSTRUCTION_JMP:
		case INSTRUCTION_EXG:
		case INSTRUCTION_UNIMPLEMENTED_1:
		case INSTRUCTION_UNIMPLEMENTED_2:
			/* Doesn't have a size. */
			break;
	}

	return operation_size;
}

#define SET_OPERAND(OPERATION_SIZE, ADDRESS_MODE, OPERAND_ADDRESS_MODE_REGISTER)\
do\
{\
	OPERAND.operation_size = OPERATION_SIZE;\
	OPERAND.address_mode = ADDRESS_MODE;\
	OPERAND.address_mode_register = OPERAND_ADDRESS_MODE_REGISTER;\
} while(0)

#define OPERAND decoded_opcode->operands[0]

static void GetSourceOperand(DecodedOpcode* const decoded_opcode, const SplitOpcode* const opcode)
{
	/* Obtain source value. */
	switch (decoded_opcode->instruction)
	{
		case INSTRUCTION_ORI_TO_CCR:
		case INSTRUCTION_ORI_TO_SR:
		case INSTRUCTION_ORI:
		case INSTRUCTION_ANDI_TO_CCR:
		case INSTRUCTION_ANDI_TO_SR:
		case INSTRUCTION_ANDI:
		case INSTRUCTION_SUBI:
		case INSTRUCTION_ADDI:
		case INSTRUCTION_EORI_TO_CCR:
		case INSTRUCTION_EORI_TO_SR:
		case INSTRUCTION_EORI:
		case INSTRUCTION_CMPI:
		case INSTRUCTION_LINK:
		case INSTRUCTION_STOP:
			/* Immediate value (any size). */
			SET_OPERAND(decoded_opcode->size, OPERAND_ADDRESS_MODE_SPECIAL, OPERAND_ADDRESS_MODE_REGISTER_SPECIAL_IMMEDIATE);
			break;

		case INSTRUCTION_BTST_DYNAMIC:
		case INSTRUCTION_BCHG_DYNAMIC:
		case INSTRUCTION_BCLR_DYNAMIC:
		case INSTRUCTION_BSET_DYNAMIC:
		case INSTRUCTION_EOR:
			/* Secondary data register. */
			SET_OPERAND(decoded_opcode->size, OPERAND_ADDRESS_MODE_DATA_REGISTER, opcode->secondary_register);
			break;

		case INSTRUCTION_BTST_STATIC:
		case INSTRUCTION_BCHG_STATIC:
		case INSTRUCTION_BCLR_STATIC:
		case INSTRUCTION_BSET_STATIC:
			/* Immediate value (byte). */
			SET_OPERAND(OPERATION_SIZE_BYTE, OPERAND_ADDRESS_MODE_SPECIAL, OPERAND_ADDRESS_MODE_REGISTER_SPECIAL_IMMEDIATE);
			break;

		case INSTRUCTION_PEA:
		case INSTRUCTION_JSR:
		case INSTRUCTION_JMP:
		case INSTRUCTION_LEA:
			/* Memory address */
			SET_OPERAND(OPERATION_SIZE_NONE, (OperandAddressMode)opcode->primary_address_mode, opcode->primary_register); /* 0 is a special value that means to obtain the address rather than the data at that address. */
			break;

		case INSTRUCTION_MOVE_FROM_SR:
			OPERAND.address_mode = OPERAND_ADDRESS_MODE_STATUS_REGISTER;
			break;

		case INSTRUCTION_TRAP:
			SET_OPERAND(OPERATION_SIZE_WORD, OPERAND_ADDRESS_MODE_EMBEDDED_IMMEDIATE, opcode->raw & 0xF);
			break;

		case INSTRUCTION_BRA_WORD:
		case INSTRUCTION_BSR_WORD:
		case INSTRUCTION_BCC_WORD:
			/* Immediate value (word). */
			SET_OPERAND(OPERATION_SIZE_WORD, OPERAND_ADDRESS_MODE_SPECIAL, OPERAND_ADDRESS_MODE_REGISTER_SPECIAL_IMMEDIATE_ADDRESS);
			break;

		case INSTRUCTION_SBCD:
		case INSTRUCTION_ABCD:
		case INSTRUCTION_SUBX:
		case INSTRUCTION_ADDX:
			SET_OPERAND(decoded_opcode->size, (opcode->raw & 0x0008) != 0 ? OPERAND_ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_PREDECREMENT : OPERAND_ADDRESS_MODE_DATA_REGISTER, opcode->primary_register);
			break;

		case INSTRUCTION_OR:
		case INSTRUCTION_SUB:
		case INSTRUCTION_AND:
		case INSTRUCTION_ADD:
			/* Primary address mode or secondary data register, based on direction bit. */
			if (opcode->bit_8)
				SET_OPERAND(decoded_opcode->size, OPERAND_ADDRESS_MODE_DATA_REGISTER, opcode->secondary_register);
			else
				SET_OPERAND(decoded_opcode->size, (OperandAddressMode)opcode->primary_address_mode, opcode->primary_register);

			break;

		case INSTRUCTION_CMPM:
			SET_OPERAND(decoded_opcode->size, OPERAND_ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_POSTINCREMENT, opcode->primary_register);
			break;

		case INSTRUCTION_MOVEA:
		case INSTRUCTION_MOVE:
		case INSTRUCTION_MOVE_TO_CCR:
		case INSTRUCTION_MOVE_TO_SR:
		case INSTRUCTION_CHK:
		case INSTRUCTION_CMP:
		case INSTRUCTION_TST:
		case INSTRUCTION_SUBA:
		case INSTRUCTION_CMPA:
		case INSTRUCTION_ADDA:
			/* Primary address mode. */
			SET_OPERAND(decoded_opcode->size, (OperandAddressMode)opcode->primary_address_mode, opcode->primary_register);
			break;

		case INSTRUCTION_DIVU:
		case INSTRUCTION_DIVS:
		case INSTRUCTION_MULU:
		case INSTRUCTION_MULS:
			/* Primary address mode, hardcoded to word-size. */
			SET_OPERAND(OPERATION_SIZE_WORD, (OperandAddressMode)opcode->primary_address_mode, opcode->primary_register);
			break;

		case INSTRUCTION_BRA_SHORT:
		case INSTRUCTION_BSR_SHORT:
		case INSTRUCTION_BCC_SHORT:
			SET_OPERAND(OPERATION_SIZE_BYTE, OPERAND_ADDRESS_MODE_EMBEDDED_IMMEDIATE_ADDRESS, CC_SIGN_EXTEND_UINT(7, opcode->raw));
			break;

		case INSTRUCTION_MOVEQ:
			SET_OPERAND(OPERATION_SIZE_BYTE, OPERAND_ADDRESS_MODE_EMBEDDED_IMMEDIATE, CC_SIGN_EXTEND_UINT(7, opcode->raw));
			break;

		case INSTRUCTION_DBCC:
			SET_OPERAND(OPERATION_SIZE_WORD, OPERAND_ADDRESS_MODE_DATA_REGISTER, opcode->primary_register);
			break;

		case INSTRUCTION_ADDAQ:
		case INSTRUCTION_SUBAQ:
		case INSTRUCTION_ADDQ:
		case INSTRUCTION_SUBQ:
		case INSTRUCTION_ASD_REGISTER:
		case INSTRUCTION_LSD_REGISTER:
		case INSTRUCTION_ROXD_REGISTER:
		case INSTRUCTION_ROD_REGISTER:
			SET_OPERAND(OPERATION_SIZE_WORD, OPERAND_ADDRESS_MODE_EMBEDDED_IMMEDIATE, ((opcode->secondary_register - 1u) & 7u) + 1u); /* A little math trick to turn 0 into 8 */
			break;

		case INSTRUCTION_MOVE_USP:
			if ((opcode->raw & 8) != 0)
				SET_OPERAND(OPERATION_SIZE_LONGWORD, OPERAND_ADDRESS_MODE_USER_STACK_POINTER, 0);
			else
				SET_OPERAND(OPERATION_SIZE_LONGWORD, OPERAND_ADDRESS_MODE_ADDRESS_REGISTER, opcode->primary_register);

			break;

		case INSTRUCTION_MOVEP:
			if ((opcode->bits_6_and_7 & 2) != 0)
				SET_OPERAND(OPERATION_SIZE_NONE, OPERAND_ADDRESS_MODE_DATA_REGISTER, opcode->secondary_register);
			else
				SET_OPERAND(OPERATION_SIZE_NONE, OPERAND_ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_DISPLACEMENT, opcode->primary_register);

			break;

		case INSTRUCTION_MOVEM:
			if ((opcode->raw & 0x0400) != 0)
			{
				SET_OPERAND(OPERATION_SIZE_NONE, (OperandAddressMode)opcode->primary_address_mode, opcode->primary_register); /* OPERATION_SIZE_NONE is a special value that means to obtain the address rather than the data at that address. */
			}
			else
			{
				if (opcode->primary_address_mode == ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_PREDECREMENT)
					SET_OPERAND(OPERATION_SIZE_WORD, OPERAND_ADDRESS_MODE_SPECIAL, OPERAND_ADDRESS_MODE_REGISTER_SPECIAL_REGISTER_LIST_REVERSED);
				else
					SET_OPERAND(OPERATION_SIZE_WORD, OPERAND_ADDRESS_MODE_SPECIAL, OPERAND_ADDRESS_MODE_REGISTER_SPECIAL_REGISTER_LIST);
			}

			break;

		case INSTRUCTION_NEGX:
		case INSTRUCTION_CLR:
		case INSTRUCTION_NEG:
		case INSTRUCTION_NOT:
		case INSTRUCTION_EXT:
		case INSTRUCTION_NBCD:
		case INSTRUCTION_SWAP:
		case INSTRUCTION_ILLEGAL:
		case INSTRUCTION_TAS:
		case INSTRUCTION_UNLK:
		case INSTRUCTION_RESET:
		case INSTRUCTION_NOP:
		case INSTRUCTION_RTE:
		case INSTRUCTION_RTS:
		case INSTRUCTION_TRAPV:
		case INSTRUCTION_RTR:
		case INSTRUCTION_SCC:
		case INSTRUCTION_EXG:
		case INSTRUCTION_ASD_MEMORY:
		case INSTRUCTION_LSD_MEMORY:
		case INSTRUCTION_ROXD_MEMORY:
		case INSTRUCTION_ROD_MEMORY:
		case INSTRUCTION_UNIMPLEMENTED_1:
		case INSTRUCTION_UNIMPLEMENTED_2:
			/* Doesn't have a source address mode to decode. */
			OPERAND.address_mode = OPERAND_ADDRESS_MODE_NONE;
			break;
	}
}

#undef OPERAND
#define OPERAND decoded_opcode->operands[1]

static void GetDestinationOperand(DecodedOpcode* const decoded_opcode, const SplitOpcode* const opcode)
{
	/* Decode destination address mode */
	switch (decoded_opcode->instruction)
	{
		case INSTRUCTION_EXT:
		case INSTRUCTION_SWAP:
		case INSTRUCTION_ASD_REGISTER:
		case INSTRUCTION_LSD_REGISTER:
		case INSTRUCTION_ROXD_REGISTER:
		case INSTRUCTION_ROD_REGISTER:
			/* Data register (primary) */
			SET_OPERAND(decoded_opcode->size, OPERAND_ADDRESS_MODE_DATA_REGISTER, opcode->primary_register);
			break;

		case INSTRUCTION_MOVEQ:
		case INSTRUCTION_CMP:
			/* Data register (secondary) */
			SET_OPERAND(decoded_opcode->size, OPERAND_ADDRESS_MODE_DATA_REGISTER, opcode->secondary_register);
			break;

		case INSTRUCTION_DIVU:
		case INSTRUCTION_DIVS:
		case INSTRUCTION_MULU:
		case INSTRUCTION_MULS:
			/* Full secondary data register */
			SET_OPERAND(OPERATION_SIZE_LONGWORD, OPERAND_ADDRESS_MODE_DATA_REGISTER, opcode->secondary_register);
			break;

		case INSTRUCTION_MOVE:
			/* Secondary address mode */
			SET_OPERAND(decoded_opcode->size, (OperandAddressMode)opcode->secondary_address_mode, opcode->secondary_register);
			break;

		case INSTRUCTION_SBCD:
		case INSTRUCTION_SUBX:
		case INSTRUCTION_ABCD:
		case INSTRUCTION_ADDX:
			SET_OPERAND(decoded_opcode->size, (opcode->raw & 0x0008) != 0 ? OPERAND_ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_PREDECREMENT : OPERAND_ADDRESS_MODE_DATA_REGISTER, opcode->secondary_register);
			break;

		case INSTRUCTION_OR:
		case INSTRUCTION_SUB:
		case INSTRUCTION_AND:
		case INSTRUCTION_ADD:
			/* Primary address mode or secondary data register, based on direction bit */
			if (opcode->bit_8)
				SET_OPERAND(decoded_opcode->size, (OperandAddressMode)opcode->primary_address_mode, opcode->primary_register);
			else
				SET_OPERAND(decoded_opcode->size, OPERAND_ADDRESS_MODE_DATA_REGISTER, opcode->secondary_register);

			break;

		case INSTRUCTION_LEA:
		case INSTRUCTION_SUBA:
		case INSTRUCTION_CMPA:
		case INSTRUCTION_ADDA:
		case INSTRUCTION_MOVEA:
			/* Full secondary address register */
			SET_OPERAND(OPERATION_SIZE_LONGWORD, OPERAND_ADDRESS_MODE_ADDRESS_REGISTER, opcode->secondary_register);
			break;

		case INSTRUCTION_CMPM:
			SET_OPERAND(decoded_opcode->size, OPERAND_ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_POSTINCREMENT, opcode->secondary_register);
			break;

		case INSTRUCTION_ORI:
		case INSTRUCTION_ANDI:
		case INSTRUCTION_SUBI:
		case INSTRUCTION_ADDI:
		case INSTRUCTION_EORI:
		case INSTRUCTION_CMPI:
		case INSTRUCTION_BTST_STATIC:
		case INSTRUCTION_BCHG_STATIC:
		case INSTRUCTION_BCLR_STATIC:
		case INSTRUCTION_BSET_STATIC:
		case INSTRUCTION_BTST_DYNAMIC:
		case INSTRUCTION_BCHG_DYNAMIC:
		case INSTRUCTION_BCLR_DYNAMIC:
		case INSTRUCTION_BSET_DYNAMIC:
		case INSTRUCTION_NEGX:
		case INSTRUCTION_NEG:
		case INSTRUCTION_NOT:
		case INSTRUCTION_NBCD:
		case INSTRUCTION_TAS:
		case INSTRUCTION_MOVE_FROM_SR:
		case INSTRUCTION_CLR:
		case INSTRUCTION_ADDAQ:
		case INSTRUCTION_SUBAQ:
		case INSTRUCTION_ADDQ:
		case INSTRUCTION_SUBQ:
		case INSTRUCTION_SCC:
		case INSTRUCTION_EOR:
		case INSTRUCTION_ASD_MEMORY:
		case INSTRUCTION_LSD_MEMORY:
		case INSTRUCTION_ROXD_MEMORY:
		case INSTRUCTION_ROD_MEMORY:
			/* Using primary address mode */
			SET_OPERAND(decoded_opcode->size, (OperandAddressMode)opcode->primary_address_mode, opcode->primary_register);
			break;

		case INSTRUCTION_ORI_TO_CCR:
		case INSTRUCTION_ANDI_TO_CCR:
		case INSTRUCTION_EORI_TO_CCR:
		case INSTRUCTION_MOVE_TO_CCR:
			OPERAND.address_mode = OPERAND_ADDRESS_MODE_CONDITION_CODE_REGISTER;
			break;

		case INSTRUCTION_ORI_TO_SR:
		case INSTRUCTION_ANDI_TO_SR:
		case INSTRUCTION_EORI_TO_SR:
		case INSTRUCTION_MOVE_TO_SR:
			OPERAND.address_mode = OPERAND_ADDRESS_MODE_STATUS_REGISTER;
			break;

		case INSTRUCTION_MOVEM:
			if ((opcode->raw & 0x0400) != 0)
				SET_OPERAND(OPERATION_SIZE_WORD, OPERAND_ADDRESS_MODE_SPECIAL, OPERAND_ADDRESS_MODE_REGISTER_SPECIAL_REGISTER_LIST);
			else
				SET_OPERAND(OPERATION_SIZE_NONE, (OperandAddressMode)opcode->primary_address_mode, opcode->primary_register); /* OPERATION_SIZE_NONE is a special value that means to obtain the address rather than the data at that address. */

			break;

		case INSTRUCTION_MOVEP:
			if ((opcode->bits_6_and_7 & 2) != 0)
				SET_OPERAND(OPERATION_SIZE_NONE, OPERAND_ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_DISPLACEMENT, opcode->primary_register);
			else
				SET_OPERAND(OPERATION_SIZE_NONE, OPERAND_ADDRESS_MODE_DATA_REGISTER, opcode->secondary_register);

			break;

		case INSTRUCTION_DBCC:
			/* Immediate value (any size). */
			SET_OPERAND(decoded_opcode->size, OPERAND_ADDRESS_MODE_SPECIAL, OPERAND_ADDRESS_MODE_REGISTER_SPECIAL_IMMEDIATE_ADDRESS);
			break;

		case INSTRUCTION_MOVE_USP:
			if ((opcode->raw & 8) != 0)
				SET_OPERAND(OPERATION_SIZE_LONGWORD, OPERAND_ADDRESS_MODE_ADDRESS_REGISTER, opcode->primary_register);
			else
				SET_OPERAND(OPERATION_SIZE_LONGWORD, OPERAND_ADDRESS_MODE_USER_STACK_POINTER, 0);

			break;

		case INSTRUCTION_PEA:
		case INSTRUCTION_ILLEGAL:
		case INSTRUCTION_TRAP:
		case INSTRUCTION_LINK:
		case INSTRUCTION_UNLK:
		case INSTRUCTION_RESET:
		case INSTRUCTION_NOP:
		case INSTRUCTION_STOP:
		case INSTRUCTION_RTE:
		case INSTRUCTION_RTS:
		case INSTRUCTION_TRAPV:
		case INSTRUCTION_RTR:
		case INSTRUCTION_JSR:
		case INSTRUCTION_JMP:
		case INSTRUCTION_CHK:
		case INSTRUCTION_BRA_SHORT:
		case INSTRUCTION_BRA_WORD:
		case INSTRUCTION_BSR_SHORT:
		case INSTRUCTION_BSR_WORD:
		case INSTRUCTION_BCC_SHORT:
		case INSTRUCTION_BCC_WORD:
		case INSTRUCTION_EXG:
		case INSTRUCTION_TST:
		case INSTRUCTION_UNIMPLEMENTED_1:
		case INSTRUCTION_UNIMPLEMENTED_2:
			/* Doesn't have a destination address mode to decode. */
			OPERAND.address_mode = OPERAND_ADDRESS_MODE_NONE;
			break;
	}
}

#undef OPERAND
#undef SET_OPERAND

void DecodeOpcodeAndOperands(DecodedOpcode* const decoded_opcode, SplitOpcode* const split_opcode, const unsigned int opcode)
{
	decoded_opcode->instruction = DecodeOpcode(split_opcode, opcode);
	decoded_opcode->size = GetSize(decoded_opcode->instruction, split_opcode);
	GetSourceOperand(decoded_opcode, split_opcode);
	GetDestinationOperand(decoded_opcode, split_opcode);
}
