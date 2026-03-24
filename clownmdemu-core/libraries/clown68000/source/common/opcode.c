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

static Instruction GetInstruction(const SplitOpcode *opcode)
{
	Instruction instruction;

	instruction = INSTRUCTION_ILLEGAL;

	switch ((opcode->raw >> 12) & 0xF)
	{
		case 0x0:
			if (opcode->bit_8)
			{
				if (opcode->primary_address_mode == ADDRESS_MODE_ADDRESS_REGISTER)
				{
					instruction = INSTRUCTION_MOVEP;
				}
				else
				{
					switch (opcode->bits_6_and_7)
					{
						case 0:
							instruction = INSTRUCTION_BTST_DYNAMIC;
							break;

						case 1:
							instruction = INSTRUCTION_BCHG_DYNAMIC;
							break;

						case 2:
							instruction = INSTRUCTION_BCLR_DYNAMIC;
							break;

						case 3:
							instruction = INSTRUCTION_BSET_DYNAMIC;
							break;
					}
				}
			}
			else
			{
				switch (opcode->secondary_register)
				{
					case 0:
						if (opcode->primary_address_mode == ADDRESS_MODE_SPECIAL && opcode->primary_register == ADDRESS_MODE_REGISTER_SPECIAL_IMMEDIATE)
						{
							switch (opcode->bits_6_and_7)
							{
								case 0:
									instruction = INSTRUCTION_ORI_TO_CCR;
									break;

								case 1:
									instruction = INSTRUCTION_ORI_TO_SR;
									break;
							}
						}
						else
						{
							instruction = INSTRUCTION_ORI;
						}

						break;

					case 1:
						if (opcode->primary_address_mode == ADDRESS_MODE_SPECIAL && opcode->primary_register == ADDRESS_MODE_REGISTER_SPECIAL_IMMEDIATE)
						{
							switch (opcode->bits_6_and_7)
							{
								case 0:
									instruction = INSTRUCTION_ANDI_TO_CCR;
									break;

								case 1:
									instruction = INSTRUCTION_ANDI_TO_SR;
									break;
							}
						}
						else
						{
							instruction = INSTRUCTION_ANDI;
						}

						break;

					case 2:
						instruction = INSTRUCTION_SUBI;
						break;

					case 3:
						instruction = INSTRUCTION_ADDI;
						break;

					case 4:
						switch (opcode->bits_6_and_7)
						{
							case 0:
								instruction = INSTRUCTION_BTST_STATIC;
								break;

							case 1:
								instruction = INSTRUCTION_BCHG_STATIC;
								break;

							case 2:
								instruction = INSTRUCTION_BCLR_STATIC;
								break;

							case 3:
								instruction = INSTRUCTION_BSET_STATIC;
								break;
						}

						break;

					case 5:
						if (opcode->primary_address_mode == ADDRESS_MODE_SPECIAL && opcode->primary_register == ADDRESS_MODE_REGISTER_SPECIAL_IMMEDIATE)
						{
							switch (opcode->bits_6_and_7)
							{
								case 0:
									instruction = INSTRUCTION_EORI_TO_CCR;
									break;

								case 1:
									instruction = INSTRUCTION_EORI_TO_SR;
									break;
							}
						}
						else
						{
							instruction = INSTRUCTION_EORI;
						}

						break;

					case 6:
						instruction = INSTRUCTION_CMPI;
						break;
				}
			}

			break;

		case 0x1:
		case 0x2:
		case 0x3:
			if ((opcode->raw & 0x01C0) == 0x0040)
				instruction = INSTRUCTION_MOVEA;
			else
				instruction = INSTRUCTION_MOVE;

			break;

		case 0x4:
			if (opcode->bit_8)
			{
				switch (opcode->bits_6_and_7)
				{
					case 3:
						instruction = INSTRUCTION_LEA;
						break;

					case 2:
						instruction = INSTRUCTION_CHK;
						break;

					default:
						break;
				}
			}
			else if ((opcode->raw & 0x0800) == 0)
			{
				if (opcode->bits_6_and_7 == 3)
				{
					switch (opcode->secondary_register)
					{
						case 0:
							instruction = INSTRUCTION_MOVE_FROM_SR;
							break;

						case 2:
							instruction = INSTRUCTION_MOVE_TO_CCR;
							break;

						case 3:
							instruction = INSTRUCTION_MOVE_TO_SR;
							break;
					}
				}
				else
				{
					switch (opcode->secondary_register)
					{
						case 0:
							instruction = INSTRUCTION_NEGX;
							break;

						case 1:
							instruction = INSTRUCTION_CLR;
							break;

						case 2:
							instruction = INSTRUCTION_NEG;
							break;

						case 3:
							instruction = INSTRUCTION_NOT;
							break;
					}
				}
			}
			else if ((opcode->raw & 0x0200) == 0)
			{
				if ((opcode->raw & 0x01B8) == 0x0080)
					instruction = INSTRUCTION_EXT;
				else if ((opcode->raw & 0x01C0) == 0x0000)
					instruction = INSTRUCTION_NBCD;
				else if ((opcode->raw & 0x01F8) == 0x0040)
					instruction = INSTRUCTION_SWAP;
				else if ((opcode->raw & 0x01C0) == 0x0040)
					instruction = INSTRUCTION_PEA;
				else if ((opcode->raw & 0x0B80) == 0x0880)
					instruction = INSTRUCTION_MOVEM;
			}
			else if (opcode->raw == 0x4AFA || opcode->raw == 0x4AFB || opcode->raw == 0x4AFC)
			{
				instruction = INSTRUCTION_ILLEGAL;
			}
			else if ((opcode->raw & 0x0FC0) == 0x0AC0)
			{
				instruction = INSTRUCTION_TAS;
			}
			else if ((opcode->raw & 0x0F00) == 0x0A00)
			{
				instruction = INSTRUCTION_TST;
			}
			else if ((opcode->raw & 0x0FF0) == 0x0E40)
			{
				instruction = INSTRUCTION_TRAP;
			}
			else if ((opcode->raw & 0x0FF8) == 0x0E50)
			{
				instruction = INSTRUCTION_LINK;
			}
			else if ((opcode->raw & 0x0FF8) == 0x0E58)
			{
				instruction = INSTRUCTION_UNLK;
			}
			else if ((opcode->raw & 0x0FF0) == 0x0E60)
			{
				instruction = INSTRUCTION_MOVE_USP;
			}
			else if ((opcode->raw & 0x0FF8) == 0x0E70)
			{
				switch (opcode->primary_register)
				{
					case 0:
						instruction = INSTRUCTION_RESET;
						break;

					case 1:
						instruction = INSTRUCTION_NOP;
						break;

					case 2:
						instruction = INSTRUCTION_STOP;
						break;

					case 3:
						instruction = INSTRUCTION_RTE;
						break;

					case 5:
						instruction = INSTRUCTION_RTS;
						break;

					case 6:
						instruction = INSTRUCTION_TRAPV;
						break;

					case 7:
						instruction = INSTRUCTION_RTR;
						break;
				}
			}
			else if ((opcode->raw & 0x0FC0) == 0x0E80)
			{
				instruction = INSTRUCTION_JSR;
			}
			else if ((opcode->raw & 0x0FC0) == 0x0EC0)
			{
				instruction = INSTRUCTION_JMP;
			}

			break;

		case 0x5:
			if (opcode->bits_6_and_7 == 3)
			{
				if (opcode->primary_address_mode == ADDRESS_MODE_ADDRESS_REGISTER)
					instruction = INSTRUCTION_DBCC;
				else
					instruction = INSTRUCTION_SCC;
			}
			else
			{
				if (opcode->primary_address_mode == ADDRESS_MODE_ADDRESS_REGISTER)
					instruction = opcode->bit_8 ? INSTRUCTION_SUBAQ : INSTRUCTION_ADDAQ;
				else
					instruction = opcode->bit_8 ? INSTRUCTION_SUBQ : INSTRUCTION_ADDQ;
			}

			break;

		case 0x6:
			if (opcode->secondary_register != 0)
				instruction = (opcode->raw & 0x00FF) == 0 ? INSTRUCTION_BCC_WORD : INSTRUCTION_BCC_SHORT;
			else if (opcode->bit_8)
				instruction = (opcode->raw & 0x00FF) == 0 ? INSTRUCTION_BSR_WORD : INSTRUCTION_BSR_SHORT;
			else
				instruction = (opcode->raw & 0x00FF) == 0 ? INSTRUCTION_BRA_WORD : INSTRUCTION_BRA_SHORT;

			break;

		case 0x7:
			instruction = INSTRUCTION_MOVEQ;
			break;

		case 0x8:
			if (opcode->bits_6_and_7 == 3)
			{
				if (opcode->bit_8)
					instruction = INSTRUCTION_DIVS;
				else
					instruction = INSTRUCTION_DIVU;
			}
			else
			{
				if ((opcode->raw & 0x0170) == 0x0100)
					instruction = INSTRUCTION_SBCD;
				else
					instruction = INSTRUCTION_OR;
			}

			break;

		case 0x9:
			if (opcode->bits_6_and_7 == 3)
				instruction = INSTRUCTION_SUBA;
			else if ((opcode->raw & 0x0130) == 0x0100)
				instruction = INSTRUCTION_SUBX;
			else
				instruction = INSTRUCTION_SUB;

			break;

		case 0xA:
			instruction = INSTRUCTION_UNIMPLEMENTED_1;
			break;

		case 0xB:
			if (opcode->bits_6_and_7 == 3)
				instruction = INSTRUCTION_CMPA;
			else if (!opcode->bit_8)
				instruction = INSTRUCTION_CMP;
			else if (opcode->primary_address_mode == ADDRESS_MODE_ADDRESS_REGISTER)
				instruction = INSTRUCTION_CMPM;
			else
				instruction = INSTRUCTION_EOR;

			break;

		case 0xC:
			if (opcode->bits_6_and_7 == 3)
			{
				if (opcode->bit_8)
					instruction = INSTRUCTION_MULS;
				else
					instruction = INSTRUCTION_MULU;
			}
			else if ((opcode->raw & 0x0130) == 0x0100)
			{
				if (opcode->bits_6_and_7 == 0)
					instruction = INSTRUCTION_ABCD;
				else
					instruction = INSTRUCTION_EXG;
			}
			else
			{
				instruction = INSTRUCTION_AND;
			}

			break;

		case 0xD:
			if (opcode->bits_6_and_7 == 3)
				instruction = INSTRUCTION_ADDA;
			else if ((opcode->raw & 0x0130) == 0x0100)
				instruction = INSTRUCTION_ADDX;
			else
				instruction = INSTRUCTION_ADD;

			break;

		case 0xE:
			if (opcode->bits_6_and_7 == 3)
			{
				switch (opcode->secondary_register)
				{
					case 0:
						instruction = INSTRUCTION_ASD_MEMORY;
						break;

					case 1:
						instruction = INSTRUCTION_LSD_MEMORY;
						break;

					case 2:
						instruction = INSTRUCTION_ROXD_MEMORY;
						break;

					case 3:
						instruction = INSTRUCTION_ROD_MEMORY;
						break;
				}
			}
			else
			{
				switch (opcode->raw & 0x0018)
				{
					case 0x0000:
						instruction = INSTRUCTION_ASD_REGISTER;
						break;

					case 0x0008:
						instruction = INSTRUCTION_LSD_REGISTER;
						break;

					case 0x0010:
						instruction = INSTRUCTION_ROXD_REGISTER;
						break;

					case 0x0018:
						instruction = INSTRUCTION_ROD_REGISTER;
						break;
				}
			}

			break;

		case 0xF:
			instruction = INSTRUCTION_UNIMPLEMENTED_2;
			break;
	}

	return instruction;
}

Instruction DecodeOpcode(SplitOpcode* const split_opcode, const unsigned int opcode)
{
	split_opcode->raw = opcode;

	split_opcode->bits_6_and_7 = (split_opcode->raw >> 6) & 3;
	split_opcode->bit_8 = (split_opcode->raw & 0x100) != 0;

	split_opcode->primary_register = (split_opcode->raw >> 0) & 7;
	split_opcode->primary_address_mode = (AddressMode)((split_opcode->raw >> 3) & 7);
	split_opcode->secondary_address_mode = (AddressMode)((split_opcode->raw >> 6) & 7);
	split_opcode->secondary_register = (split_opcode->raw >> 9) & 7;

	return GetInstruction(split_opcode);
}
