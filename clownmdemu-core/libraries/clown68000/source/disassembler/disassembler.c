#include "disassembler.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "opcode.h"

typedef struct Stuff
{
	unsigned long address;
	Clown68000_Disassemble_ReadCallback read_callback;
	void *user_data;
} Stuff;

static long ReadWord(Stuff* const stuff)
{
	stuff->address += 2;
	return stuff->read_callback(stuff->user_data);
}

static size_t UnsignedHexToString(char* const buffer, const unsigned long hex)
{
	static const char lut[0x10] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

	size_t index, subindex;
	unsigned int i;

	index = 0;

	if (hex >= 0xA)
		buffer[index++] = '$';

	subindex = 0;

	for (i = 0; i < 8; ++i)
	{
		const char character = lut[(hex >> (4 * (8 - 1 - i))) & 0xF];

		if (character != '0' || subindex != 0 || i == 7)
			buffer[index + subindex++] = character;
	}

	index += subindex;

	return index;
}

static size_t SignedHexToString(char* const buffer, const unsigned long hex)
{
	if ((hex & 0x80000000) != 0)
	{
		buffer[0] = '-';
		return 1 + UnsignedHexToString(&buffer[1], 0 - hex);
	}
	else
	{
		return UnsignedHexToString(buffer, hex);
	}
}

#define GET_STRING_DATA(NAME) \
	size = sizeof(NAME) - 1; \
	string = NAME; \
	break

static size_t GetInstructionName(char* const buffer, const Instruction instruction)
{
	size_t size;
	const char *string;

	switch (instruction)
	{
		default:

		case INSTRUCTION_ABCD:
			GET_STRING_DATA("abcd");

		case INSTRUCTION_ADD:
			GET_STRING_DATA("add");

		case INSTRUCTION_ADDA:
			GET_STRING_DATA("adda");

		case INSTRUCTION_ADDAQ:
		case INSTRUCTION_ADDQ:
			GET_STRING_DATA("addq");

		case INSTRUCTION_ADDI:
			GET_STRING_DATA("addi");

		case INSTRUCTION_ADDX:
			GET_STRING_DATA("addx");

		case INSTRUCTION_AND:
			GET_STRING_DATA("and");

		case INSTRUCTION_ANDI:
		case INSTRUCTION_ANDI_TO_CCR:
		case INSTRUCTION_ANDI_TO_SR:
			GET_STRING_DATA("andi");

		case INSTRUCTION_ASD_MEMORY:
		case INSTRUCTION_ASD_REGISTER:
			GET_STRING_DATA("as");

		case INSTRUCTION_BCC_SHORT:
		case INSTRUCTION_BCC_WORD:
			GET_STRING_DATA("b");

		case INSTRUCTION_BCHG_DYNAMIC:
		case INSTRUCTION_BCHG_STATIC:
			GET_STRING_DATA("bchg");

		case INSTRUCTION_BCLR_DYNAMIC:
		case INSTRUCTION_BCLR_STATIC:
			GET_STRING_DATA("bclr");

		case INSTRUCTION_BRA_SHORT:
		case INSTRUCTION_BRA_WORD:
			GET_STRING_DATA("bra");

		case INSTRUCTION_BSET_DYNAMIC:
		case INSTRUCTION_BSET_STATIC:
			GET_STRING_DATA("bset");

		case INSTRUCTION_BSR_SHORT:
		case INSTRUCTION_BSR_WORD:
			GET_STRING_DATA("bsr");

		case INSTRUCTION_BTST_DYNAMIC:
		case INSTRUCTION_BTST_STATIC:
			GET_STRING_DATA("btst");

		case INSTRUCTION_CHK:
			GET_STRING_DATA("chk");

		case INSTRUCTION_CLR:
			GET_STRING_DATA("clr");

		case INSTRUCTION_CMP:
			GET_STRING_DATA("cmp");

		case INSTRUCTION_CMPA:
			GET_STRING_DATA("cmpa");

		case INSTRUCTION_CMPI:
			GET_STRING_DATA("cmpi");

		case INSTRUCTION_CMPM:
			GET_STRING_DATA("cmpm");

		case INSTRUCTION_DBCC:
			GET_STRING_DATA("db");

		case INSTRUCTION_DIVS:
			GET_STRING_DATA("divs");

		case INSTRUCTION_DIVU:
			GET_STRING_DATA("divu");

		case INSTRUCTION_EOR:
			GET_STRING_DATA("eor");

		case INSTRUCTION_EORI:
		case INSTRUCTION_EORI_TO_CCR:
		case INSTRUCTION_EORI_TO_SR:
			GET_STRING_DATA("eori");

		case INSTRUCTION_EXG:
			GET_STRING_DATA("exg");

		case INSTRUCTION_EXT:
			GET_STRING_DATA("ext");

		case INSTRUCTION_ILLEGAL:
			GET_STRING_DATA("illegal");

		case INSTRUCTION_JMP:
			GET_STRING_DATA("jmp");

		case INSTRUCTION_JSR:
			GET_STRING_DATA("jsr");

		case INSTRUCTION_LEA:
			GET_STRING_DATA("lea");

		case INSTRUCTION_LINK:
			GET_STRING_DATA("link");

		case INSTRUCTION_LSD_MEMORY:
		case INSTRUCTION_LSD_REGISTER:
			GET_STRING_DATA("ls");

		case INSTRUCTION_MOVE:
		case INSTRUCTION_MOVE_FROM_SR:
		case INSTRUCTION_MOVE_TO_CCR:
		case INSTRUCTION_MOVE_TO_SR:
		case INSTRUCTION_MOVE_USP:
			GET_STRING_DATA("move");

		case INSTRUCTION_MOVEA:
			GET_STRING_DATA("movea");

		case INSTRUCTION_MOVEM:
			GET_STRING_DATA("movem");

		case INSTRUCTION_MOVEP:
			GET_STRING_DATA("movep");

		case INSTRUCTION_MOVEQ:
			GET_STRING_DATA("moveq");

		case INSTRUCTION_MULS:
			GET_STRING_DATA("muls");

		case INSTRUCTION_MULU:
			GET_STRING_DATA("mulu");

		case INSTRUCTION_NBCD:
			GET_STRING_DATA("nbcd");

		case INSTRUCTION_NEG:
			GET_STRING_DATA("neg");

		case INSTRUCTION_NEGX:
			GET_STRING_DATA("negx");

		case INSTRUCTION_NOP:
			GET_STRING_DATA("nop");

		case INSTRUCTION_NOT:
			GET_STRING_DATA("not");

		case INSTRUCTION_OR:
			GET_STRING_DATA("or");

		case INSTRUCTION_ORI:
		case INSTRUCTION_ORI_TO_CCR:
		case INSTRUCTION_ORI_TO_SR:
			GET_STRING_DATA("ori");

		case INSTRUCTION_PEA:
			GET_STRING_DATA("pea");

		case INSTRUCTION_RESET:
			GET_STRING_DATA("reset");

		case INSTRUCTION_ROD_MEMORY:
		case INSTRUCTION_ROD_REGISTER:
			GET_STRING_DATA("ro");

		case INSTRUCTION_ROXD_MEMORY:
		case INSTRUCTION_ROXD_REGISTER:
			GET_STRING_DATA("rox");

		case INSTRUCTION_RTE:
			GET_STRING_DATA("rte");

		case INSTRUCTION_RTR:
			GET_STRING_DATA("rtr");

		case INSTRUCTION_RTS:
			GET_STRING_DATA("rts");

		case INSTRUCTION_SBCD:
			GET_STRING_DATA("sbcd");

		case INSTRUCTION_SCC:
			GET_STRING_DATA("s");

		case INSTRUCTION_STOP:
			GET_STRING_DATA("stop");

		case INSTRUCTION_SUB:
			GET_STRING_DATA("sub");

		case INSTRUCTION_SUBA:
			GET_STRING_DATA("suba");

		case INSTRUCTION_SUBAQ:
		case INSTRUCTION_SUBQ:
			GET_STRING_DATA("subq");

		case INSTRUCTION_SUBI:
			GET_STRING_DATA("subi");

		case INSTRUCTION_SUBX:
			GET_STRING_DATA("subx");

		case INSTRUCTION_SWAP:
			GET_STRING_DATA("swap");

		case INSTRUCTION_TAS:
			GET_STRING_DATA("tas");

		case INSTRUCTION_TRAP:
			GET_STRING_DATA("trap");

		case INSTRUCTION_TRAPV:
			GET_STRING_DATA("trapv");

		case INSTRUCTION_TST:
			GET_STRING_DATA("tst");

		case INSTRUCTION_UNLK:
			GET_STRING_DATA("unlk");

		case INSTRUCTION_UNIMPLEMENTED_1:
			GET_STRING_DATA("[invalid instruction (1)]");

		case INSTRUCTION_UNIMPLEMENTED_2:
			GET_STRING_DATA("[invalid instruction (2)]");
	}

	memcpy(buffer, string, size);
	return size;
}

static size_t GetOpcodeConditionName(char* const buffer, const unsigned int opcode)
{
	size_t size;
	const char *string;

	switch ((opcode >> 8) & 0xF)
	{
		case 0x0:
			/* True */
			GET_STRING_DATA("t");

		case 0x1:
			/* False */
			GET_STRING_DATA("f");

		case 0x2:
			/* Higher */
			GET_STRING_DATA("hi");

		case 0x3:
			/* Lower or same */
			GET_STRING_DATA("ls");

		case 0x4:
			/* Carry clear */
			GET_STRING_DATA("cc");

		case 0x5:
			/* Carry set */
			GET_STRING_DATA("cs");

		case 0x6:
			/* Not equal */
			GET_STRING_DATA("ne");

		case 0x7:
			/* Equal */
			GET_STRING_DATA("eq");

		case 0x8:
			/* Overflow clear */
			GET_STRING_DATA("vc");

		case 0x9:
			/* Overflow set */
			GET_STRING_DATA("vs");

		case 0xA:
			/* Plus */
			GET_STRING_DATA("pl");

		case 0xB:
			/* Minus */
			GET_STRING_DATA("mi");

		case 0xC:
			/* Greater or equal */
			GET_STRING_DATA("ge");

		case 0xD:
			/* Less than */
			GET_STRING_DATA("lt");

		case 0xE:
			/* Greater than */
			GET_STRING_DATA("gt");

		case 0xF:
			/* Less or equal */
			GET_STRING_DATA("le");
	}

	memcpy(buffer, string, size);
	return size;
}

#undef GET_STRING_DATA

static size_t GetAddressRegister(char* const buffer, unsigned int address_register)
{
	if (address_register == 7)
	{
		buffer[0] = 's';
		buffer[1] = 'p';
	}
	else
	{
		buffer[0] = 'a';
		buffer[1] = '0' + address_register;
	}

	return 2;
}

static size_t GetOperandName(Stuff* const stuff, char* const buffer, const DecodedOpcode* const decoded_opcode, const cc_bool operand_is_destination)
{
	size_t index = 0;
	const Operand* const operand = &decoded_opcode->operands[operand_is_destination ? 1 : 0];

	switch (operand->address_mode)
	{
		case OPERAND_ADDRESS_MODE_DATA_REGISTER:
		case OPERAND_ADDRESS_MODE_ADDRESS_REGISTER:
		case OPERAND_ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT:
		case OPERAND_ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_POSTINCREMENT:
		case OPERAND_ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_PREDECREMENT:
			if (operand->address_mode == OPERAND_ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_PREDECREMENT)
				buffer[index++] = '-';

			switch (operand->address_mode)
			{
				case OPERAND_ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT:
				case OPERAND_ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_POSTINCREMENT:
				case OPERAND_ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_PREDECREMENT:
					buffer[index++] = '(';
					break;

				default:
					break;
			}

			switch (operand->address_mode)
			{
				case OPERAND_ADDRESS_MODE_DATA_REGISTER:
					buffer[index++] = 'd';
					buffer[index++] = '0' + operand->address_mode_register;
					break;

				case OPERAND_ADDRESS_MODE_ADDRESS_REGISTER:
				case OPERAND_ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT:
				case OPERAND_ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_POSTINCREMENT:
				case OPERAND_ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_PREDECREMENT:
					index += GetAddressRegister(&buffer[index], operand->address_mode_register);
					break;

				default:
					break;
			}

			switch (operand->address_mode)
			{
				case OPERAND_ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT:
				case OPERAND_ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_POSTINCREMENT:
				case OPERAND_ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_PREDECREMENT:
					buffer[index++] = ')';
					break;

				default:
					break;
			}

			if (operand->address_mode == OPERAND_ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_POSTINCREMENT)
				buffer[index++] = '+';

			break;

		case OPERAND_ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_DISPLACEMENT:
		{
			const unsigned long data = ReadWord(stuff);
			index += SignedHexToString(&buffer[index], CC_SIGN_EXTEND_ULONG(15, data));
			buffer[index++] = '(';
			index += GetAddressRegister(&buffer[index], operand->address_mode_register);
			buffer[index++] = ')';
			break;
		}

		case OPERAND_ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_INDEX:
		{
			const unsigned long data = ReadWord(stuff);
			const unsigned long literal = CC_SIGN_EXTEND_ULONG(7, data);

			if (literal != 0)
				index += SignedHexToString(&buffer[index], literal);

			buffer[index++] = '(';
			index += GetAddressRegister(&buffer[index], operand->address_mode_register);
			buffer[index++] = ',';
			if ((data & 0x8000) != 0)
			{
				index += GetAddressRegister(&buffer[index], ((data >> 12) & 7));
			}
			else
			{
				buffer[index++] = 'd';
				buffer[index++] = '0' + ((data >> 12) & 7);
			}
			buffer[index++] = '.';
			buffer[index++] = (data & 0x800) != 0 ? 'l' : 'w';
			buffer[index++] = ')';

			break;
		}

		case OPERAND_ADDRESS_MODE_SPECIAL:
		{
			const unsigned long address = stuff->address;
			const unsigned long data = ReadWord(stuff);

			switch (operand->address_mode_register)
			{
				case OPERAND_ADDRESS_MODE_REGISTER_SPECIAL_ABSOLUTE_SHORT:
					buffer[index++] = '(';
					index += UnsignedHexToString(&buffer[index], CC_SIGN_EXTEND_ULONG(15, data));
					buffer[index++] = ')';
					buffer[index++] = '.';
					buffer[index++] = 'w';
					break;

				case OPERAND_ADDRESS_MODE_REGISTER_SPECIAL_ABSOLUTE_LONG:
					buffer[index++] = '(';
					index += UnsignedHexToString(&buffer[index], (data << 16) | ReadWord(stuff));
					buffer[index++] = ')';
					buffer[index++] = '.';
					buffer[index++] = 'l';
					break;

				case OPERAND_ADDRESS_MODE_REGISTER_SPECIAL_PROGRAM_COUNTER_WITH_DISPLACEMENT:
					index += UnsignedHexToString(&buffer[index], address + CC_SIGN_EXTEND_ULONG(15, data));
					buffer[index++] = '(';
					buffer[index++] = 'p';
					buffer[index++] = 'c';
					buffer[index++] = ')';
					break;

				case OPERAND_ADDRESS_MODE_REGISTER_SPECIAL_PROGRAM_COUNTER_WITH_INDEX:
					index += UnsignedHexToString(&buffer[index], address + CC_SIGN_EXTEND_ULONG(7, data));
					buffer[index++] = '(';
					buffer[index++] = 'p';
					buffer[index++] = 'c';
					buffer[index++] = ',';

					if ((data & 0x8000) != 0)
					{
						index += GetAddressRegister(&buffer[index], ((data >> 12) & 7));
					}
					else
					{
						buffer[index++] = 'd';
						buffer[index++] = '0' + ((data >> 12) & 7);
					}

					buffer[index++] = '.';
					buffer[index++] = (data & 0x800) != 0 ? 'l' : 'w';
					buffer[index++] = ')';
					break;

				case OPERAND_ADDRESS_MODE_REGISTER_SPECIAL_IMMEDIATE:
				{
					unsigned long full_data, sign_extended_full_data;

					buffer[index++] = '#';

					switch (operand->operation_size)
					{
						case OPERATION_SIZE_LONGWORD:
							full_data = (data << 16) | ReadWord(stuff);
							sign_extended_full_data = full_data;
							break;

						case OPERATION_SIZE_WORD:
							full_data = data;
							sign_extended_full_data = CC_SIGN_EXTEND_ULONG(15, data);
							break;

						case OPERATION_SIZE_BYTE:
						default:
							full_data = data & 0xFF;
							sign_extended_full_data = CC_SIGN_EXTEND_ULONG(7, data);
							break;
					}

					if (sign_extended_full_data >= (unsigned long)-0x10)
						index += SignedHexToString(&buffer[index], sign_extended_full_data);
					else
						index += UnsignedHexToString(&buffer[index], full_data);

					break;
				}

				case OPERAND_ADDRESS_MODE_REGISTER_SPECIAL_IMMEDIATE_ADDRESS:
					if (operand->operation_size == OPERATION_SIZE_LONGWORD)
						index += UnsignedHexToString(&buffer[index], address + ((data << 16) | ReadWord(stuff)));
					else
						index += UnsignedHexToString(&buffer[index], address + CC_SIGN_EXTEND_ULONG(15, data));

					break;

				case OPERAND_ADDRESS_MODE_REGISTER_SPECIAL_REGISTER_LIST:
				case OPERAND_ADDRESS_MODE_REGISTER_SPECIAL_REGISTER_LIST_REVERSED:
				{
					const unsigned int xor_mask = operand->address_mode_register == OPERAND_ADDRESS_MODE_REGISTER_SPECIAL_REGISTER_LIST_REVERSED ? 0xF : 0;

					unsigned int i;
					cc_bool first;

					first = cc_true;

					for (i = 0; i < 16; ++i)
					{
						if ((data & (1 << (i ^ xor_mask))) != 0)
						{
							static const char registers[][3] = {
								"d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7",
								"a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
							};

							if (first)
								first = cc_false;
							else
								buffer[index++] = '/';

							buffer[index++] = registers[i][0];
							buffer[index++] = registers[i][1];

							++i;

							if (i < 16 && (data & (1 << (i ^ xor_mask))) != 0)
							{
								for (i = i + 1; i < 16; ++i)
									if ((data & (1 << (i ^ xor_mask))) == 0)
										break;

								--i;

								buffer[index++] = '-';
								buffer[index++] = registers[i][0];
								buffer[index++] = registers[i][1];
							}
						}
					}

					break;
				}
			}

			break;
		}

		case OPERAND_ADDRESS_MODE_STATUS_REGISTER:
			buffer[index++] = 's';
			buffer[index++] = 'r';
			break;

		case OPERAND_ADDRESS_MODE_CONDITION_CODE_REGISTER:
			buffer[index++] = 'c';
			buffer[index++] = 'c';
			buffer[index++] = 'r';
			break;

		case OPERAND_ADDRESS_MODE_NONE:
			assert(0);
			break;

		case OPERAND_ADDRESS_MODE_EMBEDDED_IMMEDIATE:
			buffer[index++] = '#';
			index += SignedHexToString(&buffer[index], CC_SIGN_EXTEND_ULONG(operand->operation_size == OPERATION_SIZE_BYTE ? 7 : operand->operation_size == OPERATION_SIZE_WORD ? 15 : 31, operand->address_mode_register));
			break;

		case OPERAND_ADDRESS_MODE_EMBEDDED_IMMEDIATE_ADDRESS:
			index += UnsignedHexToString(&buffer[index], stuff->address + CC_SIGN_EXTEND_ULONG(operand->operation_size == OPERATION_SIZE_BYTE ? 7 : operand->operation_size == OPERATION_SIZE_WORD ? 15 : 31, operand->address_mode_register));
			break;

		case OPERAND_ADDRESS_MODE_USER_STACK_POINTER:
			buffer[index++] = 'u';
			buffer[index++] = 's';
			buffer[index++] = 'p';
			break;
	}

	return index;
}

void Clown68000_Disassemble(const unsigned long address, const unsigned int max_instructions, const Clown68000_Disassemble_ReadCallback read_callback, const Clown68000_Disassemble_PrintCallback print_callback, const void* const user_data)
{
	unsigned int i;
	Stuff stuff;

	stuff.address = address;
	stuff.read_callback = read_callback;
	stuff.user_data = (void*)user_data;

	for (i = 0; i < max_instructions; ++i)
	{
		char string_buffer[0x80];
		size_t index;
		DecodedOpcode decoded_opcode;
		SplitOpcode split_opcode;
		long opcode;

		sprintf(string_buffer, "%08lX: ", stuff.address);
		index = 8 + 2;

		opcode = ReadWord(&stuff);

		if (opcode == -1)
			return;

		DecodeOpcodeAndOperands(&decoded_opcode, &split_opcode, opcode);

		index += GetInstructionName(&string_buffer[index], decoded_opcode.instruction);

		/* Instruction name suffix and special operands. */
		switch (decoded_opcode.instruction)
		{
			case INSTRUCTION_BCC_SHORT:
			case INSTRUCTION_BCC_WORD:
			case INSTRUCTION_DBCC:
			case INSTRUCTION_SCC:
				index += GetOpcodeConditionName(&string_buffer[index], opcode);
				break;

			case INSTRUCTION_ASD_MEMORY:
			case INSTRUCTION_ASD_REGISTER:
			case INSTRUCTION_LSD_MEMORY:
			case INSTRUCTION_LSD_REGISTER:
			case INSTRUCTION_ROD_MEMORY:
			case INSTRUCTION_ROD_REGISTER:
			case INSTRUCTION_ROXD_MEMORY:
			case INSTRUCTION_ROXD_REGISTER:
				string_buffer[index++] = split_opcode.bit_8 ? 'l' : 'r';
				break;

			default:
				break;
		}

		switch (decoded_opcode.instruction)
		{
			case INSTRUCTION_DBCC:
			case INSTRUCTION_LEA:
			case INSTRUCTION_MOVEQ:
			case INSTRUCTION_PEA:
			case INSTRUCTION_EXG:
			case INSTRUCTION_SWAP:
			case INSTRUCTION_LINK:
			case INSTRUCTION_STOP:
				break;

			default:
				if (decoded_opcode.size != OPERATION_SIZE_NONE)
				{
					string_buffer[index++] = '.';
					string_buffer[index++] = decoded_opcode.size == OPERATION_SIZE_SHORT ? 's' : decoded_opcode.size == OPERATION_SIZE_BYTE ? 'b' : decoded_opcode.size == OPERATION_SIZE_WORD ? 'w' : 'l';
				}

				break;
		}

		while (index < 18)
			string_buffer[index++] = ' ';

		if (decoded_opcode.operands[0].address_mode != OPERAND_ADDRESS_MODE_NONE)
			index += GetOperandName(&stuff, &string_buffer[index], &decoded_opcode, cc_false);

		if (decoded_opcode.operands[0].address_mode != OPERAND_ADDRESS_MODE_NONE && decoded_opcode.operands[1].address_mode != OPERAND_ADDRESS_MODE_NONE)
			string_buffer[index++] = ',';

		if (decoded_opcode.operands[1].address_mode != OPERAND_ADDRESS_MODE_NONE)
			index += GetOperandName(&stuff, &string_buffer[index], &decoded_opcode, cc_true);

		string_buffer[index++] = '\0';

		print_callback((void*)user_data, string_buffer);

		/* Halt when encountering a terminating instruction. */
		switch (decoded_opcode.instruction)
		{
			case INSTRUCTION_BRA_SHORT:
			case INSTRUCTION_BRA_WORD:
			case INSTRUCTION_JMP:
			case INSTRUCTION_RESET:
			case INSTRUCTION_RTE:
			case INSTRUCTION_RTR:
			case INSTRUCTION_RTS:
			case INSTRUCTION_TRAP:
			case INSTRUCTION_TRAPV:
				return;

			default:
				break;
		}
	}
}
