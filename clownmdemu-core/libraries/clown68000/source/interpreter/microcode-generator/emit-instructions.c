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

#include "emit-instructions.h"

#include <stdio.h>

#include "emit.h"
#include "instruction-properties.h"

void EmitInstructionSupervisorCheck(const Instruction instruction)
{
	if (Instruction_IsPrivileged(instruction))
	{
		Emit("/* Only allow this instruction in supervisor mode. */");
		Emit("SupervisorCheck(&stuff);");
		Emit("");
	}
}

void EmitInstructionSize(const Instruction instruction)
{
	const InstructionSize size = Instruction_GetSize(instruction);

	if (size != INSTRUCTION_SIZE_NONE)
	{
		switch (Instruction_GetSize(instruction))
		{
			case INSTRUCTION_SIZE_BYTE:
				/* Hardcoded to a byte. */
				Emit("SetSize_Byte(&stuff);");
				break;

			case INSTRUCTION_SIZE_WORD:
				/* Hardcoded to a word. */
				Emit("SetSize_Word(&stuff);");
				break;

			case INSTRUCTION_SIZE_LONGWORD:
				/* Hardcoded to a longword. */
				Emit("SetSize_Longword(&stuff);");
				break;

			case INSTRUCTION_SIZE_LONGWORD_REGISTER_BYTE_MEMORY:
				/* 4 if register - 1 if memory. */
				Emit("SetSize_LongwordRegisterByteMemory(&stuff);");
				break;

			case INSTRUCTION_SIZE_MOVE:
				/* Derived from an odd bitfield. */
				Emit("SetSize_Move(&stuff);");
				break;

			case INSTRUCTION_SIZE_EXT:
				Emit("SetSize_Ext(&stuff);");
				break;

			case INSTRUCTION_SIZE_STANDARD:
				/* Standard. */
				Emit("SetSize_Standard(&stuff);");
				break;

			case INSTRUCTION_SIZE_NONE:
				/* Doesn't have a size. */
				break;
		}

		Emit("SetMSBBitIndex(&stuff);");
		Emit("");
	}
}

void EmitInstructionSourceAddressMode(const Instruction instruction)
{
	if (Instruction_IsSourceOperandRead(instruction))
	{
		Emit("/* Decode source address mode. */");

		switch (Instruction_GetSourceOperand(instruction))
		{
			case INSTRUCTION_SOURCE_IMMEDIATE_DATA:
				/* Immediate value (any size). */
				Emit("DecodeSource_ImmediateData(&stuff);");
				break;

			case INSTRUCTION_SOURCE_DATA_REGISTER_SECONDARY:
				/* Secondary data register. */
				Emit("DecodeSource_DataRegisterSecondary(&stuff);");
				break;

			case INSTRUCTION_SOURCE_IMMEDIATE_DATA_BYTE:
				/* Immediate value (byte). */
				Emit("DecodeSource_ImmediateDataByte(&stuff);");
				break;

			case INSTRUCTION_SOURCE_MEMORY_ADDRESS_PRIMARY:
				/* Memory address */
				Emit("DecodeSource_MemoryAddressPrimary(&stuff);");
				break;

			case INSTRUCTION_SOURCE_STATUS_REGISTER:
				Emit("DecodeSource_StatusRegister(&stuff);");
				break;

			case INSTRUCTION_SOURCE_IMMEDIATE_DATA_WORD:
				/* Immediate value (word). */
				Emit("DecodeSource_ImmediateDataWord(&stuff);");
				break;

			case INSTRUCTION_SOURCE_BCD_X:
				Emit("DecodeSource_BCDX(&stuff);");
				break;

			case INSTRUCTION_SOURCE_DATA_REGISTER_SECONDARY_OR_PRIMARY_ADDRESS_MODE:
				/* Primary address mode or secondary data register, based on direction bit. */
				Emit("DecodeSource_DataRegisterSecondaryOrPrimaryAddressMode(&stuff);");
				break;

			case INSTRUCTION_SOURCE_PRIMARY_ADDRESS_MODE_SIZED:
				/* Word or longword based on bit 8. */
				Emit("DecodeSource_PrimaryAddressModeSized(&stuff);");
				break;

			case INSTRUCTION_SOURCE_ADDRESS_REGISTER_PRIMARY_POSTINCREMENT:
				Emit("DecodeSource_AddressRegisterPrimaryPostIncrement(&stuff);");
				break;

			case INSTRUCTION_SOURCE_PRIMARY_ADDRESS_MODE:
				/* Primary address mode. */
				Emit("DecodeSource_PrimaryAddressMode(&stuff);");
				break;

			case INSTRUCTION_SOURCE_PRIMARY_ADDRESS_MODE_WORD:
				/* Primary address mode, hardcoded to word-size. */
				Emit("DecodeSource_PrimaryAddressModeWord(&stuff);");
				break;

			case INSTRUCTION_SOURCE_NONE:
				/* Doesn't have a source address mode to decode. */
				break;
		}

		Emit("");
	}
}

void EmitInstructionDestinationAddressMode(const Instruction instruction)
{
	if (Instruction_IsDestinationOperandRead(instruction) || Instruction_IsDestinationOperandWritten(instruction))
	{
		Emit("/* Decode destination address mode. */");

		switch (Instruction_GetDestinationOperand(instruction))
		{
			case INSTRUCTION_DESTINATION_DATA_REGISTER_PRIMARY:
				/* Data register (primary) */
				Emit("DecodeDestination_DataRegisterPrimary(&stuff);");
				break;

			case INSTRUCTION_DESTINATION_DATA_REGISTER_SECONDARY:
				/* Data register (secondary) */
				Emit("DecodeDestination_DataRegisterSecondary(&stuff);");
				break;

			case INSTRUCTION_DESTINATION_ADDRESS_REGISTER_SECONDARY:
				/* Address register (secondary) */
				Emit("DecodeDestination_AddressRegisterSecondary(&stuff);");
				break;

			case INSTRUCTION_DESTINATION_SECONDARY_ADDRESS_MODE:
				/* Secondary address mode */
				Emit("DecodeDestination_SecondaryAddressMode(&stuff);");
				break;

			case INSTRUCTION_DESTINATION_BCD_X:
				Emit("DecodeDestination_BCDX(&stuff);");
				break;

			case INSTRUCTION_DESTINATION_DATA_REGISTER_SECONDARY_OR_PRIMARY_ADDRESS_MODE:
				/* Primary address mode or secondary data register, based on direction bit */
				Emit("DecodeDestination_DataRegisterSecondaryOrPrimaryAddressMode(&stuff);");
				break;

			case INSTRUCTION_DESTINATION_ADDRESS_REGISTER_SECONDARY_FULL:
				/* Full secondary address register */
				Emit("DecodeDestination_AddressRegisterSecondaryFull(&stuff);");
				break;

			case INSTRUCTION_DESTINATION_ADDRESS_REGISTER_SECONDARY_POSTINCREMENT:
				Emit("DecodeDestination_AddressRegisterSecondaryPostIncrement(&stuff);");
				break;

			case INSTRUCTION_DESTINATION_PRIMARY_ADDRESS_MODE:
				/* Using primary address mode */
				Emit("DecodeDestination_PrimaryAddressMode(&stuff);");
				break;

			case INSTRUCTION_DESTINATION_CONDITION_CODE_REGISTER:
				Emit("DecodeDestination_ConditionCodeRegister(&stuff);");
				break;

			case INSTRUCTION_DESTINATION_STATUS_REGISTER:
				Emit("DecodeDestination_StatusRegister(&stuff);");
				break;

			case INSTRUCTION_DESTINATION_MOVEM:
				/* Memory address */
				Emit("DecodeDestination_MOVEM(&stuff);");
				break;

			case INSTRUCTION_DESTINATION_MOVEP:
				/* Memory address */
				Emit("DecodeDestination_MOVEP(&stuff);");
				break;

			case INSTRUCTION_DESTINATION_NONE:
				/* Doesn't have a destination address mode to decode. */
				break;
		}

		Emit("");
	}
}

void EmitInstructionReadSourceOperand(const Instruction instruction)
{
	if (Instruction_IsSourceOperandRead(instruction))
	{
		Emit("/* Read source operand. */");
		Emit("ReadSource(&stuff);");
		Emit("");
	}
}

void EmitInstructionReadDestinationOperand(const Instruction instruction)
{
	if (Instruction_IsDestinationOperandRead(instruction))
	{
		Emit("/* Read destination operand. */");
		Emit("ReadDestination(&stuff);");
		Emit("");
	}
}

void EmitInstructionAction(const Instruction instruction)
{
	Emit("/* Do the actual instruction. */");

	switch (Instruction_GetAction(instruction))
	{
		case INSTRUCTION_ACTION_OR:
			Emit("Action_OR(&stuff);");
			break;

		case INSTRUCTION_ACTION_AND:
			Emit("Action_AND(&stuff);");
			break;

		case INSTRUCTION_ACTION_CMPA:
			Emit("Action_CMPA(&stuff);");
			break;

		case INSTRUCTION_ACTION_CMP:
			Emit("Action_CMP(&stuff);");
			break;

		case INSTRUCTION_ACTION_CMPM:
			Emit("Action_CMPM(&stuff);");
			break;

		case INSTRUCTION_ACTION_CMPI:
			Emit("Action_CMPI(&stuff);");
			break;

		case INSTRUCTION_ACTION_SUBA:
			Emit("Action_SUBA(&stuff);");
			break;

		case INSTRUCTION_ACTION_SUBQ:
			Emit("Action_SUBQ(&stuff);");
			break;

		case INSTRUCTION_ACTION_SUB:
			Emit("Action_SUB(&stuff);");
			break;

		case INSTRUCTION_ACTION_ADDA:
			Emit("Action_ADDA(&stuff);");
			break;

		case INSTRUCTION_ACTION_ADDQ:
			Emit("Action_ADDQ(&stuff);");
			break;

		case INSTRUCTION_ACTION_ADD:
			Emit("Action_ADD(&stuff);");
			break;

		case INSTRUCTION_ACTION_EOR:
			Emit("Action_EOR(&stuff);");
			break;

		case INSTRUCTION_ACTION_TST:
			Emit("Action_TST(&stuff);");
			break;

		case INSTRUCTION_ACTION_BCHG:
			Emit("Action_BCHG(&stuff);");
			break;

		case INSTRUCTION_ACTION_BCLR:
			Emit("Action_BCLR(&stuff);");
			break;

		case INSTRUCTION_ACTION_BSET:
			Emit("Action_BSET(&stuff);");
			break;

		case INSTRUCTION_ACTION_BTST:
			Emit("Action_BTST(&stuff);");
			break;

		case INSTRUCTION_ACTION_MOVEP:
			Emit("Action_MOVEP(&stuff);");
			break;

		case INSTRUCTION_ACTION_MOVEA:
			Emit("Action_MOVEA(&stuff);");
			break;

		case INSTRUCTION_ACTION_MOVE:
			Emit("Action_MOVE(&stuff);");
			break;

		case INSTRUCTION_ACTION_LINK:
			Emit("Action_LINK(&stuff);");
			break;

		case INSTRUCTION_ACTION_UNLK:
			Emit("Action_UNLK(&stuff);");
			break;

		case INSTRUCTION_ACTION_NEGX:
			Emit("Action_NEGX(&stuff);");
			break;

		case INSTRUCTION_ACTION_CLR:
			Emit("Action_CLR(&stuff);");
			break;

		case INSTRUCTION_ACTION_NEG:
			Emit("Action_NEG(&stuff);");
			break;

		case INSTRUCTION_ACTION_NOT:
			Emit("Action_NOT(&stuff);");
			break;

		case INSTRUCTION_ACTION_EXT:
			Emit("Action_EXT(&stuff);");
			break;

		case INSTRUCTION_ACTION_NBCD:
			Emit("Action_NBCD(&stuff);");
			break;

		case INSTRUCTION_ACTION_SWAP:
			Emit("Action_SWAP(&stuff);");
			break;

		case INSTRUCTION_ACTION_PEA:
			Emit("Action_PEA(&stuff);");
			break;

		case INSTRUCTION_ACTION_ILLEGAL:
			Emit("Action_ILLEGAL(&stuff);");
			break;

		case INSTRUCTION_ACTION_TAS:
			Emit("Action_TAS(&stuff);");
			break;

		case INSTRUCTION_ACTION_TRAP:
			Emit("Action_TRAP(&stuff);");
			break;

		case INSTRUCTION_ACTION_MOVE_USP:
			Emit("Action_MOVE_USP(&stuff);");
			break;

		case INSTRUCTION_ACTION_RESET:
			Emit("Action_RESET(&stuff);");
			break;

		case INSTRUCTION_ACTION_STOP:
			Emit("Action_STOP(&stuff);");
			break;

		case INSTRUCTION_ACTION_RTE:
			Emit("Action_RTE(&stuff);");
			break;

		case INSTRUCTION_ACTION_RTS:
			Emit("Action_RTS(&stuff);");
			break;

		case INSTRUCTION_ACTION_TRAPV:
			Emit("Action_TRAPV(&stuff);");
			break;

		case INSTRUCTION_ACTION_RTR:
			Emit("Action_RTR(&stuff);");
			break;

		case INSTRUCTION_ACTION_JSR:
			Emit("Action_JSR(&stuff);");
			break;

		case INSTRUCTION_ACTION_JMP:
			Emit("Action_JMP(&stuff);");
			break;

		case INSTRUCTION_ACTION_LEA:
			Emit("Action_LEA(&stuff);");
			break;

		case INSTRUCTION_ACTION_MOVEM:
			Emit("Action_MOVEM(&stuff);");
			break;

		case INSTRUCTION_ACTION_CHK:
			Emit("Action_CHK(&stuff);");
			break;

		case INSTRUCTION_ACTION_SCC:
			Emit("Action_SCC(&stuff);");
			break;

		case INSTRUCTION_ACTION_DBCC:
			Emit("Action_DBCC(&stuff);");
			break;

		case INSTRUCTION_ACTION_BRA_SHORT:
			Emit("Action_BRA_SHORT(&stuff);");
			break;

		case INSTRUCTION_ACTION_BRA_WORD:
			Emit("Action_BRA_WORD(&stuff);");
			break;

		case INSTRUCTION_ACTION_BSR_SHORT:
			Emit("Action_BSR_SHORT(&stuff);");
			break;

		case INSTRUCTION_ACTION_BSR_WORD:
			Emit("Action_BSR_WORD(&stuff);");
			break;

		case INSTRUCTION_ACTION_BCC_SHORT:
			Emit("Action_BCC_SHORT(&stuff);");
			break;

		case INSTRUCTION_ACTION_BCC_WORD:
			Emit("Action_BCC_WORD(&stuff);");
			break;

		case INSTRUCTION_ACTION_MOVEQ:
			Emit("Action_MOVEQ(&stuff);");
			break;

		case INSTRUCTION_ACTION_DIVS:
			Emit("Action_DIVS(&stuff);");
			break;

		case INSTRUCTION_ACTION_DIVU:
			Emit("Action_DIVU(&stuff);");
			break;

		case INSTRUCTION_ACTION_SBCD:
			Emit("Action_SBCD(&stuff);");
			break;

		case INSTRUCTION_ACTION_SUBX:
			Emit("Action_SUBX(&stuff);");
			break;

		case INSTRUCTION_ACTION_MULS:
			Emit("Action_MULS(&stuff);");
			break;

		case INSTRUCTION_ACTION_MULU:
			Emit("Action_MULU(&stuff);");
			break;

		case INSTRUCTION_ACTION_ABCD:
			Emit("Action_ABCD(&stuff);");
			break;

		case INSTRUCTION_ACTION_EXG:
			Emit("Action_EXG(&stuff);");
			break;

		case INSTRUCTION_ACTION_ADDX:
			Emit("Action_ADDX(&stuff);");
			break;

		case INSTRUCTION_ACTION_ASD_MEMORY:
			Emit("Action_ASD_MEMORY(&stuff);");
			break;

		case INSTRUCTION_ACTION_ASD_REGISTER:
			Emit("Action_ASD_REGISTER(&stuff);");
			break;

		case INSTRUCTION_ACTION_LSD_MEMORY:
			Emit("Action_LSD_MEMORY(&stuff);");
			break;

		case INSTRUCTION_ACTION_LSD_REGISTER:
			Emit("Action_LSD_REGISTER(&stuff);");
			break;

		case INSTRUCTION_ACTION_ROD_MEMORY:
			Emit("Action_ROD_MEMORY(&stuff);");
			break;

		case INSTRUCTION_ACTION_ROD_REGISTER:
			Emit("Action_ROD_REGISTER(&stuff);");
			break;

		case INSTRUCTION_ACTION_ROXD_MEMORY:
			Emit("Action_ROXD_MEMORY(&stuff);");
			break;

		case INSTRUCTION_ACTION_ROXD_REGISTER:
			Emit("Action_ROXD_REGISTER(&stuff);");
			break;

		case INSTRUCTION_ACTION_UNIMPLEMENTED_1:
			Emit("Action_UNIMPLEMENTED_1(&stuff);");
			break;

		case INSTRUCTION_ACTION_UNIMPLEMENTED_2:
			Emit("Action_UNIMPLEMENTED_2(&stuff);");
			break;

		case INSTRUCTION_ACTION_NOP:
			Emit("Action_NOP(&stuff);");
			break;
	}

	Emit("");
}

void EmitInstructionWriteDestinationOperand(const Instruction instruction)
{
	if (Instruction_IsDestinationOperandWritten(instruction))
	{
		Emit("/* Write destination operand. */");
		Emit("WriteDestination(&stuff);");
		Emit("");
	}
}

void EmitInstructionConditionCodes(const Instruction instruction)
{
	Emit("/* Update the condition codes in the following order: */");
	Emit("/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */");
	Emit("");

	Emit("/* Update CARRY condition code */");
	switch (Instruction_GetCarryModifier(instruction))
	{
		case INSTRUCTION_CARRY_STANDARD_CARRY:
			Emit("Carry_StandardCarry(&stuff);");
			break;

		case INSTRUCTION_CARRY_STANDARD_BORROW:
			Emit("Carry_StandardBorrow(&stuff);");
			break;

		case INSTRUCTION_CARRY_NEG:
			Emit("Carry_NEG(&stuff);");
			break;

		case INSTRUCTION_CARRY_CLEAR:
			Emit("Carry_Clear(&stuff);");
			break;

		case INSTRUCTION_CARRY_UNDEFINED:
			Emit("/* Undefined */");
			break;

		case INSTRUCTION_CARRY_UNAFFECTED:
			Emit("/* Unaffected */");
			break;
	}

	Emit("/* Update OVERFLOW condition code */");
	switch (Instruction_GetOverflowModifier(instruction))
	{
		case INSTRUCTION_OVERFLOW_ADD:
			Emit("Overflow_ADD(&stuff);");
			break;

		case INSTRUCTION_OVERFLOW_SUB:
			Emit("Overflow_SUB(&stuff);");
			break;

		case INSTRUCTION_OVERFLOW_NEG:
			Emit("Overflow_NEG(&stuff);");
			break;

		case INSTRUCTION_OVERFLOW_CLEARED:
			Emit("Overflow_Clear(&stuff);");
			break;

		case INSTRUCTION_OVERFLOW_UNDEFINED:
			Emit("/* Undefined */");
			break;

		case INSTRUCTION_OVERFLOW_UNAFFECTED:
			Emit("/* Unaffected */");
			break;
	}

	Emit("/* Update ZERO condition code */");
	switch (Instruction_GetZeroModifier(instruction))
	{
		case INSTRUCTION_ZERO_CLEAR_IF_NONZERO_UNAFFECTED_OTHERWISE:
			Emit("Zero_ClearIfNonZeroUnaffectedOtherwise(&stuff);");
			break;

		case INSTRUCTION_ZERO_SET_IF_ZERO_CLEAR_OTHERWISE:
			Emit("Zero_SetIfZeroClearOtherwise(&stuff);");
			break;

		case INSTRUCTION_ZERO_UNDEFINED:
			Emit("/* Undefined */");
			break;

		case INSTRUCTION_ZERO_UNAFFECTED:
			Emit("/* Unaffected */");
			break;
	}

	Emit("/* Update NEGATIVE condition code */");
	switch (Instruction_GetNegativeModifier(instruction))
	{
		case INSTRUCTION_NEGATIVE_SET_IF_NEGATIVE_CLEAR_OTHERWISE:
			Emit("Negative_SetIfNegativeClearOtherwise(&stuff);");
			break;

		case INSTRUCTION_NEGATIVE_UNAFFECTED:
			Emit("/* Unaffected */");
			break;
	}

	Emit("/* Update EXTEND condition code */");
	switch (Instruction_GetExtendModifier(instruction))
	{
		case INSTRUCTION_EXTEND_SET_TO_CARRY:
			Emit("Extend_SetToCarry(&stuff);");
			break;

		case INSTRUCTION_EXTEND_UNAFFECTED:
			Emit("/* Unaffected */");
			break;
	}

	Emit("");
}
