#include "common.h"

void ClownZ80_DecodeInstructionMetadata(ClownZ80_InstructionMetadata* const metadata, const ClownZ80_InstructionMode instruction_mode, const ClownZ80_RegisterMode register_mode, const cc_u8l opcode)
{
	static const ClownZ80_Operand registers[8] = {CLOWNZ80_OPERAND_B, CLOWNZ80_OPERAND_C, CLOWNZ80_OPERAND_D, CLOWNZ80_OPERAND_E, CLOWNZ80_OPERAND_H, CLOWNZ80_OPERAND_L, CLOWNZ80_OPERAND_HL_INDIRECT, CLOWNZ80_OPERAND_A};
	static const ClownZ80_Operand register_pairs_1[4] = {CLOWNZ80_OPERAND_BC, CLOWNZ80_OPERAND_DE, CLOWNZ80_OPERAND_HL, CLOWNZ80_OPERAND_SP};
	static const ClownZ80_Operand register_pairs_2[4] = {CLOWNZ80_OPERAND_BC, CLOWNZ80_OPERAND_DE, CLOWNZ80_OPERAND_HL, CLOWNZ80_OPERAND_AF};
	static const ClownZ80_Opcode arithmetic_logic_opcodes[8] = {CLOWNZ80_OPCODE_ADD_A, CLOWNZ80_OPCODE_ADC_A, CLOWNZ80_OPCODE_SUB, CLOWNZ80_OPCODE_SBC_A, CLOWNZ80_OPCODE_AND, CLOWNZ80_OPCODE_XOR, CLOWNZ80_OPCODE_OR, CLOWNZ80_OPCODE_CP};
	static const ClownZ80_Opcode rotate_shift_opcodes[8] = {CLOWNZ80_OPCODE_RLC, CLOWNZ80_OPCODE_RRC, CLOWNZ80_OPCODE_RL, CLOWNZ80_OPCODE_RR, CLOWNZ80_OPCODE_SLA, CLOWNZ80_OPCODE_SRA, CLOWNZ80_OPCODE_SLL, CLOWNZ80_OPCODE_SRL};
	static const ClownZ80_Opcode block_opcodes[4][4] = {
		{CLOWNZ80_OPCODE_LDI,  CLOWNZ80_OPCODE_LDD,  CLOWNZ80_OPCODE_LDIR, CLOWNZ80_OPCODE_LDDR},
		{CLOWNZ80_OPCODE_CPI,  CLOWNZ80_OPCODE_CPD,  CLOWNZ80_OPCODE_CPIR, CLOWNZ80_OPCODE_CPDR},
		{CLOWNZ80_OPCODE_INI,  CLOWNZ80_OPCODE_IND,  CLOWNZ80_OPCODE_INIR, CLOWNZ80_OPCODE_INDR},
		{CLOWNZ80_OPCODE_OUTI, CLOWNZ80_OPCODE_OUTD, CLOWNZ80_OPCODE_OTIR, CLOWNZ80_OPCODE_OTDR}
	};

	const cc_u16f x = (opcode >> 6) & 3;
	const cc_u16f y = (opcode >> 3) & 7;
	const cc_u16f z = (opcode >> 0) & 7;
	const cc_u16f p = (y >> 1) & 3;
	const cc_bool q = (y & 1) != 0;

	cc_u16f i;

	metadata->has_displacement = cc_false;

	metadata->operands[0] = CLOWNZ80_OPERAND_NONE;
	metadata->operands[1] = CLOWNZ80_OPERAND_NONE;

	switch (instruction_mode)
	{
		case CLOWNZ80_INSTRUCTION_MODE_NORMAL:
			switch (x)
			{
				case 0:
					switch (z)
					{
						case 0:
							switch (y)
							{
								case 0:
									metadata->opcode = CLOWNZ80_OPCODE_NOP;
									break;

								case 1:
									metadata->opcode = CLOWNZ80_OPCODE_EX_AF_AF;
									break;

								case 2:
									metadata->opcode = CLOWNZ80_OPCODE_DJNZ;
									metadata->operands[0] = CLOWNZ80_OPERAND_LITERAL_8BIT;
									break;

								case 3:
									metadata->opcode = CLOWNZ80_OPCODE_JR_UNCONDITIONAL;
									metadata->operands[0] = CLOWNZ80_OPERAND_LITERAL_8BIT;
									break;

								case 4:
								case 5:
								case 6:
								case 7:
									metadata->opcode = CLOWNZ80_OPCODE_JR_CONDITIONAL;
									metadata->operands[0] = CLOWNZ80_OPERAND_LITERAL_8BIT;
									metadata->condition = y - 4;
									break;
							}

							break;

						case 1:
							if (!q)
							{
								metadata->opcode = CLOWNZ80_OPCODE_LD_16BIT;
								metadata->operands[0] = CLOWNZ80_OPERAND_LITERAL_16BIT;
								metadata->operands[1] = register_pairs_1[p];
							}
							else
							{
								metadata->opcode = CLOWNZ80_OPCODE_ADD_HL;
								metadata->operands[0] = register_pairs_1[p];
								metadata->operands[1] = CLOWNZ80_OPERAND_HL;
							}

							break;

						case 2:
						{
							static const ClownZ80_Operand operands[4] = {CLOWNZ80_OPERAND_BC_INDIRECT, CLOWNZ80_OPERAND_DE_INDIRECT, CLOWNZ80_OPERAND_ADDRESS, CLOWNZ80_OPERAND_ADDRESS};

							const ClownZ80_Operand operand_a = p == 2 ? CLOWNZ80_OPERAND_HL : CLOWNZ80_OPERAND_A;
							const ClownZ80_Operand operand_b = operands[p];

							metadata->opcode = p == 2 ? CLOWNZ80_OPCODE_LD_16BIT : CLOWNZ80_OPCODE_LD_8BIT;

							if (!q)
							{
								metadata->operands[0] = operand_a;
								metadata->operands[1] = operand_b;
							}
							else
							{
								metadata->operands[0] = operand_b;
								metadata->operands[1] = operand_a;
							}

							break;
						}

						case 3:
							if (!q)
								metadata->opcode = CLOWNZ80_OPCODE_INC_16BIT;
							else
								metadata->opcode = CLOWNZ80_OPCODE_DEC_16BIT;

							metadata->operands[1] = register_pairs_1[p];
							break;

						case 4:
							metadata->opcode = CLOWNZ80_OPCODE_INC_8BIT;
							metadata->operands[1] = registers[y];
							break;

						case 5:
							metadata->opcode = CLOWNZ80_OPCODE_DEC_8BIT;
							metadata->operands[1] = registers[y];
							break;

						case 6:
							metadata->opcode = CLOWNZ80_OPCODE_LD_8BIT;
							metadata->operands[0] = CLOWNZ80_OPERAND_LITERAL_8BIT;
							metadata->operands[1] = registers[y];
							break;

						case 7:
						{
							static const ClownZ80_Opcode opcodes[8] = {CLOWNZ80_OPCODE_RLCA, CLOWNZ80_OPCODE_RRCA, CLOWNZ80_OPCODE_RLA, CLOWNZ80_OPCODE_RRA, CLOWNZ80_OPCODE_DAA, CLOWNZ80_OPCODE_CPL, CLOWNZ80_OPCODE_SCF, CLOWNZ80_OPCODE_CCF};
							metadata->opcode = opcodes[y];
							break;
						}
					}

					break;

				case 1:
					if (z == 6 && y == 6)
					{
						metadata->opcode = CLOWNZ80_OPCODE_HALT;
					}
					else
					{
						metadata->opcode = CLOWNZ80_OPCODE_LD_8BIT;
						metadata->operands[0] = registers[z];
						metadata->operands[1] = registers[y];
					}

					break;

				case 2:
					metadata->opcode = arithmetic_logic_opcodes[y];
					metadata->operands[0] = registers[z];
					break;

				case 3:
					switch (z)
					{
						case 0:
							metadata->opcode = CLOWNZ80_OPCODE_RET_CONDITIONAL;
							metadata->condition = y;
							break;

						case 1:
							if (!q)
							{
								metadata->opcode = CLOWNZ80_OPCODE_POP;
								metadata->operands[1] = register_pairs_2[p];
							}
							else
							{
								switch (p)
								{
									case 0:
										metadata->opcode = CLOWNZ80_OPCODE_RET_UNCONDITIONAL;
										break;

									case 1:
										metadata->opcode = CLOWNZ80_OPCODE_EXX;
										break;

									case 2:
										metadata->opcode = CLOWNZ80_OPCODE_JP_HL;
										metadata->operands[0] = CLOWNZ80_OPERAND_HL;
										break;

									case 3:
										metadata->opcode = CLOWNZ80_OPCODE_LD_SP_HL;
										metadata->operands[0] = CLOWNZ80_OPERAND_HL;
										break;
								}
							}

							break;

						case 2:
							metadata->opcode = CLOWNZ80_OPCODE_JP_CONDITIONAL;
							metadata->condition = y;
							metadata->operands[0] = CLOWNZ80_OPERAND_LITERAL_16BIT;
							break;

						case 3:
							switch (y)
							{
								case 0:
									metadata->opcode = CLOWNZ80_OPCODE_JP_UNCONDITIONAL;
									metadata->operands[0] = CLOWNZ80_OPERAND_LITERAL_16BIT;
									break;

								case 1:
									metadata->opcode = CLOWNZ80_OPCODE_CB_PREFIX;

									if (register_mode != CLOWNZ80_REGISTER_MODE_HL)
										metadata->has_displacement = cc_true;

									break;

								case 2:
									metadata->opcode = CLOWNZ80_OPCODE_OUT;
									metadata->operands[0] = CLOWNZ80_OPERAND_LITERAL_8BIT;
									break;

								case 3:
									metadata->opcode = CLOWNZ80_OPCODE_IN;
									metadata->operands[0] = CLOWNZ80_OPERAND_LITERAL_8BIT;
									break;

								case 4:
									metadata->opcode = CLOWNZ80_OPCODE_EX_SP_HL;
									metadata->operands[1] = CLOWNZ80_OPERAND_HL;
									break;

								case 5:
									metadata->opcode = CLOWNZ80_OPCODE_EX_DE_HL;
									break;

								case 6:
									metadata->opcode = CLOWNZ80_OPCODE_DI;
									break;

								case 7:
									metadata->opcode = CLOWNZ80_OPCODE_EI;
									break;
							}
					
							break;

						case 4:
							metadata->opcode = CLOWNZ80_OPCODE_CALL_CONDITIONAL;
							metadata->condition = y;
							metadata->operands[0] = CLOWNZ80_OPERAND_LITERAL_16BIT;
							break;

						case 5:
							if (!q)
							{
								metadata->opcode = CLOWNZ80_OPCODE_PUSH;
								metadata->operands[0] = register_pairs_2[p];
							}
							else
							{
								switch (p)
								{
									case 0:
										metadata->opcode = CLOWNZ80_OPCODE_CALL_UNCONDITIONAL;
										metadata->operands[0] = CLOWNZ80_OPERAND_LITERAL_16BIT;
										break;

									case 1:
										metadata->opcode = CLOWNZ80_OPCODE_DD_PREFIX;
										break;

									case 2:
										metadata->opcode = CLOWNZ80_OPCODE_ED_PREFIX;
										break;

									case 3:
										metadata->opcode = CLOWNZ80_OPCODE_FD_PREFIX;
										break;
								}
							}

							break;

						case 6:
							metadata->opcode = arithmetic_logic_opcodes[y];
							metadata->operands[0] = CLOWNZ80_OPERAND_LITERAL_8BIT;
							break;

						case 7:
							metadata->opcode = CLOWNZ80_OPCODE_RST;
							metadata->embedded_literal = y * 8;
							break;
					}

					break;
			}

			break;

		case CLOWNZ80_INSTRUCTION_MODE_BITS:
			switch (x)
			{
				case 0:
					metadata->opcode = rotate_shift_opcodes[y];
					metadata->operands[1] = registers[z];
					break;

				case 1:
					metadata->opcode = CLOWNZ80_OPCODE_BIT;
					metadata->operands[1] = registers[z];
					metadata->embedded_literal = 1u << y;
					break;

				case 2:
					metadata->opcode = CLOWNZ80_OPCODE_RES;
					metadata->operands[1] = registers[z];
					metadata->embedded_literal = ~(1u << y);
					break;

				case 3:
					metadata->opcode = CLOWNZ80_OPCODE_SET;
					metadata->operands[1] = registers[z];
					metadata->embedded_literal = 1u << y;
					break;
			}

			break;

		case CLOWNZ80_INSTRUCTION_MODE_MISC:
			switch (x)
			{
				case 0:
				case 3:
					/* Invalid instruction. */
					metadata->opcode = CLOWNZ80_OPCODE_NOP;
					break;

				case 1:
					switch (z)
					{
						case 0:
							if (y != 6)
								metadata->opcode = CLOWNZ80_OPCODE_IN_REGISTER;
							else
								metadata->opcode = CLOWNZ80_OPCODE_IN_NO_REGISTER;

							break;

						case 1:
							if (y != 6)
								metadata->opcode = CLOWNZ80_OPCODE_OUT_REGISTER;
							else
								metadata->opcode = CLOWNZ80_OPCODE_OUT_NO_REGISTER;

							break;

						case 2:
							if (!q)
								metadata->opcode = CLOWNZ80_OPCODE_SBC_HL;
							else
								metadata->opcode = CLOWNZ80_OPCODE_ADC_HL;

							metadata->operands[0] = register_pairs_1[p];
							metadata->operands[1] = CLOWNZ80_OPERAND_HL;

							break;

						case 3:
							metadata->opcode = CLOWNZ80_OPCODE_LD_16BIT;

							if (!q)
							{
								metadata->operands[0] = register_pairs_1[p];
								metadata->operands[1] = CLOWNZ80_OPERAND_ADDRESS;
							}
							else
							{
								metadata->operands[0] = CLOWNZ80_OPERAND_ADDRESS;
								metadata->operands[1] = register_pairs_1[p];
							}

							break;

						case 4:
							metadata->opcode = CLOWNZ80_OPCODE_NEG;
							metadata->operands[0] = CLOWNZ80_OPERAND_A;
							metadata->operands[1] = CLOWNZ80_OPERAND_A;
							break;

						case 5:
							if (y != 1)
								metadata->opcode = CLOWNZ80_OPCODE_RETN;
							else
								metadata->opcode = CLOWNZ80_OPCODE_RETI;

							break;

						case 6:
						{
							static const cc_u16f interrupt_modes[4] = {0, 0, 1, 2};

							metadata->opcode = CLOWNZ80_OPCODE_IM;
							metadata->embedded_literal = interrupt_modes[y & 3];

							break;
						}

						case 7:
						{
							static const ClownZ80_Opcode assorted_opcodes[8] = {CLOWNZ80_OPCODE_LD_I_A, CLOWNZ80_OPCODE_LD_R_A, CLOWNZ80_OPCODE_LD_A_I, CLOWNZ80_OPCODE_LD_A_R, CLOWNZ80_OPCODE_RRD, CLOWNZ80_OPCODE_RLD, CLOWNZ80_OPCODE_NOP, CLOWNZ80_OPCODE_NOP};
							metadata->opcode = assorted_opcodes[y];
							break;
						}
					}

					break;

				case 2:
					if (z <= 3 && y >= 4)
						metadata->opcode = block_opcodes[z][y - 4];
					else
						/* Invalid instruction. */
						metadata->opcode = CLOWNZ80_OPCODE_NOP;

					break;
			}
			break;
	}

	/* Convert HL to IX and IY if needed. */
	for (i = 0; i < 2; ++i)
	{
		if (metadata->operands[i ^ 1] != CLOWNZ80_OPERAND_HL_INDIRECT
		 && metadata->operands[i ^ 1] != CLOWNZ80_OPERAND_IX_INDIRECT
		 && metadata->operands[i ^ 1] != CLOWNZ80_OPERAND_IY_INDIRECT)
		{
			switch (metadata->operands[i])
			{
				default:
					break;

				case CLOWNZ80_OPERAND_H:
					switch (register_mode)
					{
						case CLOWNZ80_REGISTER_MODE_HL:
							break;

						case CLOWNZ80_REGISTER_MODE_IX:
							metadata->operands[i] = CLOWNZ80_OPERAND_IXH;
							break;

						case CLOWNZ80_REGISTER_MODE_IY:
							metadata->operands[i] = CLOWNZ80_OPERAND_IYH;
							break;
					}

					break;

				case CLOWNZ80_OPERAND_L:
					switch (register_mode)
					{
						case CLOWNZ80_REGISTER_MODE_HL:
							break;

						case CLOWNZ80_REGISTER_MODE_IX:
							metadata->operands[i] = CLOWNZ80_OPERAND_IXL;
							break;

						case CLOWNZ80_REGISTER_MODE_IY:
							metadata->operands[i] = CLOWNZ80_OPERAND_IYL;
							break;
					}

					break;

				case CLOWNZ80_OPERAND_HL:
					switch (register_mode)
					{
						case CLOWNZ80_REGISTER_MODE_HL:
							break;

						case CLOWNZ80_REGISTER_MODE_IX:
							metadata->operands[i] = CLOWNZ80_OPERAND_IX;
							break;

						case CLOWNZ80_REGISTER_MODE_IY:
							metadata->operands[i] = CLOWNZ80_OPERAND_IY;
							break;
					}

					break;

				case CLOWNZ80_OPERAND_HL_INDIRECT:
					switch (register_mode)
					{
						case CLOWNZ80_REGISTER_MODE_HL:
							break;

						case CLOWNZ80_REGISTER_MODE_IX:
							metadata->operands[i] = CLOWNZ80_OPERAND_IX_INDIRECT;
							metadata->has_displacement = cc_true;
							break;

						case CLOWNZ80_REGISTER_MODE_IY:
							metadata->operands[i] = CLOWNZ80_OPERAND_IY_INDIRECT;
							metadata->has_displacement = cc_true;
							break;
					}

					break;
			}
		}
	}
}
