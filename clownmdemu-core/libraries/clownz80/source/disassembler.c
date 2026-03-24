#include "disassembler.h"

#include <assert.h>
#include <stdio.h>

#include "common.h"

typedef struct State
{
	unsigned long address;
	ClownZ80_ReadCallback read_callback;
	CC_ATTRIBUTE_PRINTF(2, 3) ClownZ80_PrintCallback print_callback;
	void *user_data;

	ClownZ80_InstructionMetadata metadata;
} State;

static unsigned char ReadByte(State* const state)
{
	++state->address;
	return state->read_callback(state->user_data);
}

static int ReadSignedByte(State* const state)
{
	const int offset = ReadByte(state);
	return CC_SIGN_EXTEND(int, 7, offset);
}

static unsigned int ReadTwoBytes(State* const state)
{
	const unsigned int first_byte = ReadByte(state);
	const unsigned int second_byte = ReadByte(state);
	return first_byte | ((unsigned int)second_byte << 8);
}

static void PrintHexadecimal(State* const state, const unsigned int number)
{
	/* Assumes that numbers are 16-bit at most. */
	char buffer[4 + 1];

	sprintf(buffer, "%X", number);

	/* Z80 assembly requires that hexadecimal numbers start with a decimal digit. */
	if (buffer[0] > '9')
		state->print_callback(state->user_data, "0");

	state->print_callback(state->user_data, "%s", buffer);

	/* Add hexadecimal suffix only when necessary, to avoid visual clutter. */
	if (number > 9)
		state->print_callback(state->user_data, "h");
}

static unsigned int BitmaskToIndex(const unsigned int mask)
{
	switch (mask)
	{
		case 1 << 0:
			return 0;
		case 1 << 1:
			return 1;
		case 1 << 2:
			return 2;
		case 1 << 3:
			return 3;
		case 1 << 4:
			return 4;
		case 1 << 5:
			return 5;
		case 1 << 6:
			return 6;
		case 1 << 7:
			return 7;
	}

	assert(cc_false);
	return 0;
}

static const char* GetOpcodeString(const ClownZ80_Opcode opcode)
{
	switch (opcode)
	{
		case CLOWNZ80_OPCODE_NOP:
			return "nop";
		case CLOWNZ80_OPCODE_EX_AF_AF:
			return "ex";
		case CLOWNZ80_OPCODE_DJNZ:
			return "djnz";
		case CLOWNZ80_OPCODE_JR_UNCONDITIONAL:
			return "jr";
		case CLOWNZ80_OPCODE_JR_CONDITIONAL:
			return "jr";
		case CLOWNZ80_OPCODE_LD_16BIT:
			return "ld";
		case CLOWNZ80_OPCODE_ADD_HL:
			return "add";
		case CLOWNZ80_OPCODE_LD_8BIT:
			return "ld";
		case CLOWNZ80_OPCODE_INC_16BIT:
			return "inc";
		case CLOWNZ80_OPCODE_DEC_16BIT:
			return "dec";
		case CLOWNZ80_OPCODE_INC_8BIT:
			return "inc";
		case CLOWNZ80_OPCODE_DEC_8BIT:
			return "dec";
		case CLOWNZ80_OPCODE_RLCA:
			return "rlca";
		case CLOWNZ80_OPCODE_RRCA:
			return "rrca";
		case CLOWNZ80_OPCODE_RLA:
			return "rla";
		case CLOWNZ80_OPCODE_RRA:
			return "rra";
		case CLOWNZ80_OPCODE_DAA:
			return "daa";
		case CLOWNZ80_OPCODE_CPL:
			return "cpl";
		case CLOWNZ80_OPCODE_SCF:
			return "scf";
		case CLOWNZ80_OPCODE_CCF:
			return "ccf";
		case CLOWNZ80_OPCODE_HALT:
			return "halt";
		case CLOWNZ80_OPCODE_ADD_A:
			return "add";
		case CLOWNZ80_OPCODE_ADC_A:
			return "adc";
		case CLOWNZ80_OPCODE_SUB:
			return "sub";
		case CLOWNZ80_OPCODE_SBC_A:
			return "sbc";
		case CLOWNZ80_OPCODE_AND:
			return "and";
		case CLOWNZ80_OPCODE_XOR:
			return "xor";
		case CLOWNZ80_OPCODE_OR:
			return "or";
		case CLOWNZ80_OPCODE_CP:
			return "cp";
		case CLOWNZ80_OPCODE_RET_CONDITIONAL:
			return "ret";
		case CLOWNZ80_OPCODE_POP:
			return "pop";
		case CLOWNZ80_OPCODE_RET_UNCONDITIONAL:
			return "ret";
		case CLOWNZ80_OPCODE_EXX:
			return "exx";
		case CLOWNZ80_OPCODE_JP_HL:
			return "jp";
		case CLOWNZ80_OPCODE_LD_SP_HL:
			return "ld";
		case CLOWNZ80_OPCODE_JP_CONDITIONAL:
			return "jp";
		case CLOWNZ80_OPCODE_JP_UNCONDITIONAL:
			return "jp";
		case CLOWNZ80_OPCODE_CB_PREFIX:
			return "[CB PREFIX]";
		case CLOWNZ80_OPCODE_OUT:
			return "out";
		case CLOWNZ80_OPCODE_IN:
			return "in";
		case CLOWNZ80_OPCODE_EX_SP_HL:
			return "ex";
		case CLOWNZ80_OPCODE_EX_DE_HL:
			return "ex";
		case CLOWNZ80_OPCODE_DI:
			return "di";
		case CLOWNZ80_OPCODE_EI:
			return "ei";
		case CLOWNZ80_OPCODE_CALL_CONDITIONAL:
			return "call";
		case CLOWNZ80_OPCODE_PUSH:
			return "push";
		case CLOWNZ80_OPCODE_CALL_UNCONDITIONAL:
			return "call";
		case CLOWNZ80_OPCODE_DD_PREFIX:
			return "[DD PREFIX]";
		case CLOWNZ80_OPCODE_ED_PREFIX:
			return "[ED PREFIX]";
		case CLOWNZ80_OPCODE_FD_PREFIX:
			return "[FD PREFIX]";
		case CLOWNZ80_OPCODE_RST:
			return "rst";
		case CLOWNZ80_OPCODE_RLC:
			return "rlc";
		case CLOWNZ80_OPCODE_RRC:
			return "rrc";
		case CLOWNZ80_OPCODE_RL:
			return "rl";
		case CLOWNZ80_OPCODE_RR:
			return "rr";
		case CLOWNZ80_OPCODE_SLA:
			return "sla";
		case CLOWNZ80_OPCODE_SRA:
			return "sra";
		case CLOWNZ80_OPCODE_SLL:
			return "sll";
		case CLOWNZ80_OPCODE_SRL:
			return "srl";
		case CLOWNZ80_OPCODE_BIT:
			return "bit";
		case CLOWNZ80_OPCODE_RES:
			return "res";
		case CLOWNZ80_OPCODE_SET:
			return "set";
		case CLOWNZ80_OPCODE_IN_REGISTER:
			return "in";
		case CLOWNZ80_OPCODE_IN_NO_REGISTER:
			return "in";
		case CLOWNZ80_OPCODE_OUT_REGISTER:
			return "out";
		case CLOWNZ80_OPCODE_OUT_NO_REGISTER:
			return "out";
		case CLOWNZ80_OPCODE_SBC_HL:
			return "sbc";
		case CLOWNZ80_OPCODE_ADC_HL:
			return "adc";
		case CLOWNZ80_OPCODE_NEG:
			return "neg";
		case CLOWNZ80_OPCODE_RETN:
			return "retn";
		case CLOWNZ80_OPCODE_RETI:
			return "reti";
		case CLOWNZ80_OPCODE_IM:
			return "im";
		case CLOWNZ80_OPCODE_LD_I_A:
			return "ld";
		case CLOWNZ80_OPCODE_LD_R_A:
			return "ld";
		case CLOWNZ80_OPCODE_LD_A_I:
			return "ld";
		case CLOWNZ80_OPCODE_LD_A_R:
			return "ld";
		case CLOWNZ80_OPCODE_RRD:
			return "rrd";
		case CLOWNZ80_OPCODE_RLD:
			return "rld";
		case CLOWNZ80_OPCODE_LDI:
			return "ldi";
		case CLOWNZ80_OPCODE_LDD:
			return "ldd";
		case CLOWNZ80_OPCODE_LDIR:
			return "ldir";
		case CLOWNZ80_OPCODE_LDDR:
			return "lddr";
		case CLOWNZ80_OPCODE_CPI:
			return "cpi";
		case CLOWNZ80_OPCODE_CPD:
			return "cpd";
		case CLOWNZ80_OPCODE_CPIR:
			return "cpir";
		case CLOWNZ80_OPCODE_CPDR:
			return "cpdr";
		case CLOWNZ80_OPCODE_INI:
			return "ini";
		case CLOWNZ80_OPCODE_IND:
			return "ind";
		case CLOWNZ80_OPCODE_INIR:
			return "inir";
		case CLOWNZ80_OPCODE_INDR:
			return "indr";
		case CLOWNZ80_OPCODE_OUTI:
			return "outi";
		case CLOWNZ80_OPCODE_OUTD:
			return "outd";
		case CLOWNZ80_OPCODE_OTIR:
			return "otir";
		case CLOWNZ80_OPCODE_OTDR:
			return "otdr";
	}

	return "[INVALID]";
}

static const char* GetConditionString(const ClownZ80_Condition condition)
{
	switch (condition)
	{
		case CLOWNZ80_CONDITION_NOT_ZERO:
			return "nz";
		case CLOWNZ80_CONDITION_ZERO:
			return "z";
		case CLOWNZ80_CONDITION_NOT_CARRY:
			return "nc";
		case CLOWNZ80_CONDITION_CARRY:
			return "c";
		case CLOWNZ80_CONDITION_PARITY_OVERFLOW:
			return "po";
		case CLOWNZ80_CONDITION_PARITY_EQUALITY:
			return "pe";
		case CLOWNZ80_CONDITION_PLUS:
			return "p";
		case CLOWNZ80_CONDITION_MINUS:
			return "m";
	}

	return "[INVALID]";
}

static cc_bool IsTerminatingInstruction(const ClownZ80_Opcode opcode)
{
	switch (opcode)
	{
		case CLOWNZ80_OPCODE_JR_UNCONDITIONAL:
		case CLOWNZ80_OPCODE_RET_UNCONDITIONAL:
		case CLOWNZ80_OPCODE_JP_HL:
		case CLOWNZ80_OPCODE_JP_UNCONDITIONAL:
		case CLOWNZ80_OPCODE_RETN:
		case CLOWNZ80_OPCODE_RETI:
			return cc_true;

		case CLOWNZ80_OPCODE_NOP:
		case CLOWNZ80_OPCODE_EX_AF_AF:
		case CLOWNZ80_OPCODE_DJNZ:
		case CLOWNZ80_OPCODE_JR_CONDITIONAL:
		case CLOWNZ80_OPCODE_LD_16BIT:
		case CLOWNZ80_OPCODE_ADD_HL:
		case CLOWNZ80_OPCODE_LD_8BIT:
		case CLOWNZ80_OPCODE_INC_16BIT:
		case CLOWNZ80_OPCODE_DEC_16BIT:
		case CLOWNZ80_OPCODE_INC_8BIT:
		case CLOWNZ80_OPCODE_DEC_8BIT:
		case CLOWNZ80_OPCODE_RLCA:
		case CLOWNZ80_OPCODE_RRCA:
		case CLOWNZ80_OPCODE_RLA:
		case CLOWNZ80_OPCODE_RRA:
		case CLOWNZ80_OPCODE_DAA:
		case CLOWNZ80_OPCODE_CPL:
		case CLOWNZ80_OPCODE_SCF:
		case CLOWNZ80_OPCODE_CCF:
		case CLOWNZ80_OPCODE_HALT:
		case CLOWNZ80_OPCODE_ADD_A:
		case CLOWNZ80_OPCODE_ADC_A:
		case CLOWNZ80_OPCODE_SUB:
		case CLOWNZ80_OPCODE_SBC_A:
		case CLOWNZ80_OPCODE_AND:
		case CLOWNZ80_OPCODE_XOR:
		case CLOWNZ80_OPCODE_OR:
		case CLOWNZ80_OPCODE_CP:
		case CLOWNZ80_OPCODE_RET_CONDITIONAL:
		case CLOWNZ80_OPCODE_POP:
		case CLOWNZ80_OPCODE_EXX:
		case CLOWNZ80_OPCODE_LD_SP_HL:
		case CLOWNZ80_OPCODE_JP_CONDITIONAL:
		case CLOWNZ80_OPCODE_CB_PREFIX:
		case CLOWNZ80_OPCODE_OUT:
		case CLOWNZ80_OPCODE_IN:
		case CLOWNZ80_OPCODE_EX_SP_HL:
		case CLOWNZ80_OPCODE_EX_DE_HL:
		case CLOWNZ80_OPCODE_DI:
		case CLOWNZ80_OPCODE_EI:
		case CLOWNZ80_OPCODE_CALL_CONDITIONAL:
		case CLOWNZ80_OPCODE_PUSH:
		case CLOWNZ80_OPCODE_CALL_UNCONDITIONAL:
		case CLOWNZ80_OPCODE_DD_PREFIX:
		case CLOWNZ80_OPCODE_ED_PREFIX:
		case CLOWNZ80_OPCODE_FD_PREFIX:
		case CLOWNZ80_OPCODE_RST:
		case CLOWNZ80_OPCODE_RLC:
		case CLOWNZ80_OPCODE_RRC:
		case CLOWNZ80_OPCODE_RL:
		case CLOWNZ80_OPCODE_RR:
		case CLOWNZ80_OPCODE_SLA:
		case CLOWNZ80_OPCODE_SRA:
		case CLOWNZ80_OPCODE_SLL:
		case CLOWNZ80_OPCODE_SRL:
		case CLOWNZ80_OPCODE_BIT:
		case CLOWNZ80_OPCODE_RES:
		case CLOWNZ80_OPCODE_SET:
		case CLOWNZ80_OPCODE_IN_REGISTER:
		case CLOWNZ80_OPCODE_IN_NO_REGISTER:
		case CLOWNZ80_OPCODE_OUT_REGISTER:
		case CLOWNZ80_OPCODE_OUT_NO_REGISTER:
		case CLOWNZ80_OPCODE_SBC_HL:
		case CLOWNZ80_OPCODE_ADC_HL:
		case CLOWNZ80_OPCODE_NEG:
		case CLOWNZ80_OPCODE_IM:
		case CLOWNZ80_OPCODE_LD_I_A:
		case CLOWNZ80_OPCODE_LD_R_A:
		case CLOWNZ80_OPCODE_LD_A_I:
		case CLOWNZ80_OPCODE_LD_A_R:
		case CLOWNZ80_OPCODE_RRD:
		case CLOWNZ80_OPCODE_RLD:
		case CLOWNZ80_OPCODE_LDI:
		case CLOWNZ80_OPCODE_LDD:
		case CLOWNZ80_OPCODE_LDIR:
		case CLOWNZ80_OPCODE_LDDR:
		case CLOWNZ80_OPCODE_CPI:
		case CLOWNZ80_OPCODE_CPD:
		case CLOWNZ80_OPCODE_CPIR:
		case CLOWNZ80_OPCODE_CPDR:
		case CLOWNZ80_OPCODE_INI:
		case CLOWNZ80_OPCODE_IND:
		case CLOWNZ80_OPCODE_INIR:
		case CLOWNZ80_OPCODE_INDR:
		case CLOWNZ80_OPCODE_OUTI:
		case CLOWNZ80_OPCODE_OUTD:
		case CLOWNZ80_OPCODE_OTIR:
		case CLOWNZ80_OPCODE_OTDR:
			return cc_false;
	}

	return cc_false;
}

static void PrintSpecialOperands(State* const state)
{
	switch ((ClownZ80_Opcode)state->metadata.opcode)
	{
		case CLOWNZ80_OPCODE_EX_AF_AF:
			state->print_callback(state->user_data, "af,af'");
			break;

		case CLOWNZ80_OPCODE_RET_CONDITIONAL:
			state->print_callback(state->user_data, "%s", GetConditionString((ClownZ80_Condition)state->metadata.condition));
			break;

		case CLOWNZ80_OPCODE_JR_CONDITIONAL:
			state->print_callback(state->user_data, "%s,", GetConditionString((ClownZ80_Condition)state->metadata.condition));
			/* Fallthrough */
		case CLOWNZ80_OPCODE_JR_UNCONDITIONAL:
			/* Turn the offset into an absolute address, to better match how assembly is written. */
			PrintHexadecimal(state, state->address + ReadSignedByte(state));
			/* Since we manually handled the operand, blank them to prevent them from being printed a second time. */
			state->metadata.operands[0] = state->metadata.operands[1] = CLOWNZ80_OPERAND_NONE;
			break;

		case CLOWNZ80_OPCODE_JP_CONDITIONAL:
		case CLOWNZ80_OPCODE_CALL_CONDITIONAL:
			state->print_callback(state->user_data, "%s,", GetConditionString((ClownZ80_Condition)state->metadata.condition));
			break;

		case CLOWNZ80_OPCODE_EX_SP_HL:
			state->print_callback(state->user_data, "(sp),");
			break;

		case CLOWNZ80_OPCODE_EX_DE_HL:
			state->print_callback(state->user_data, "de,hl");
			break;

		case CLOWNZ80_OPCODE_RST:
		case CLOWNZ80_OPCODE_IM:
			PrintHexadecimal(state, state->metadata.embedded_literal);
			break;

		case CLOWNZ80_OPCODE_BIT:
		case CLOWNZ80_OPCODE_RES:
		case CLOWNZ80_OPCODE_SET:
			state->print_callback(state->user_data, "%u,", BitmaskToIndex(state->metadata.embedded_literal));
			break;

		case CLOWNZ80_OPCODE_LD_I_A:
			state->print_callback(state->user_data, "i,a");
			break;

		case CLOWNZ80_OPCODE_LD_R_A:
			state->print_callback(state->user_data, "r,a");
			break;

		case CLOWNZ80_OPCODE_LD_A_I:
			state->print_callback(state->user_data, "a,i");
			break;

		case CLOWNZ80_OPCODE_LD_A_R:
			state->print_callback(state->user_data, "a,r");
			break;

		case CLOWNZ80_OPCODE_ADD_A:
		case CLOWNZ80_OPCODE_ADC_A:
		case CLOWNZ80_OPCODE_SBC_A:
			state->print_callback(state->user_data, "a,");
			break;

		case CLOWNZ80_OPCODE_LD_SP_HL:
			state->print_callback(state->user_data, "sp,");
			break;

		case CLOWNZ80_OPCODE_NOP:
		case CLOWNZ80_OPCODE_DJNZ:
		case CLOWNZ80_OPCODE_LD_16BIT:
		case CLOWNZ80_OPCODE_ADD_HL:
		case CLOWNZ80_OPCODE_LD_8BIT:
		case CLOWNZ80_OPCODE_INC_16BIT:
		case CLOWNZ80_OPCODE_DEC_16BIT:
		case CLOWNZ80_OPCODE_INC_8BIT:
		case CLOWNZ80_OPCODE_DEC_8BIT:
		case CLOWNZ80_OPCODE_RLCA:
		case CLOWNZ80_OPCODE_RRCA:
		case CLOWNZ80_OPCODE_RLA:
		case CLOWNZ80_OPCODE_RRA:
		case CLOWNZ80_OPCODE_DAA:
		case CLOWNZ80_OPCODE_CPL:
		case CLOWNZ80_OPCODE_SCF:
		case CLOWNZ80_OPCODE_CCF:
		case CLOWNZ80_OPCODE_HALT:
		case CLOWNZ80_OPCODE_SUB:
		case CLOWNZ80_OPCODE_AND:
		case CLOWNZ80_OPCODE_XOR:
		case CLOWNZ80_OPCODE_OR:
		case CLOWNZ80_OPCODE_CP:
		case CLOWNZ80_OPCODE_POP:
		case CLOWNZ80_OPCODE_RET_UNCONDITIONAL:
		case CLOWNZ80_OPCODE_EXX:
		case CLOWNZ80_OPCODE_JP_HL:
		case CLOWNZ80_OPCODE_JP_UNCONDITIONAL:
		case CLOWNZ80_OPCODE_CB_PREFIX:
		case CLOWNZ80_OPCODE_OUT:
		case CLOWNZ80_OPCODE_IN:
		case CLOWNZ80_OPCODE_DI:
		case CLOWNZ80_OPCODE_EI:
		case CLOWNZ80_OPCODE_PUSH:
		case CLOWNZ80_OPCODE_CALL_UNCONDITIONAL:
		case CLOWNZ80_OPCODE_DD_PREFIX:
		case CLOWNZ80_OPCODE_ED_PREFIX:
		case CLOWNZ80_OPCODE_FD_PREFIX:
		case CLOWNZ80_OPCODE_RLC:
		case CLOWNZ80_OPCODE_RRC:
		case CLOWNZ80_OPCODE_RL:
		case CLOWNZ80_OPCODE_RR:
		case CLOWNZ80_OPCODE_SLA:
		case CLOWNZ80_OPCODE_SRA:
		case CLOWNZ80_OPCODE_SLL:
		case CLOWNZ80_OPCODE_SRL:
		case CLOWNZ80_OPCODE_IN_REGISTER:
		case CLOWNZ80_OPCODE_IN_NO_REGISTER:
		case CLOWNZ80_OPCODE_OUT_REGISTER:
		case CLOWNZ80_OPCODE_OUT_NO_REGISTER:
		case CLOWNZ80_OPCODE_SBC_HL:
		case CLOWNZ80_OPCODE_ADC_HL:
		case CLOWNZ80_OPCODE_NEG:
		case CLOWNZ80_OPCODE_RETN:
		case CLOWNZ80_OPCODE_RETI:
		case CLOWNZ80_OPCODE_RRD:
		case CLOWNZ80_OPCODE_RLD:
		case CLOWNZ80_OPCODE_LDI:
		case CLOWNZ80_OPCODE_LDD:
		case CLOWNZ80_OPCODE_LDIR:
		case CLOWNZ80_OPCODE_LDDR:
		case CLOWNZ80_OPCODE_CPI:
		case CLOWNZ80_OPCODE_CPD:
		case CLOWNZ80_OPCODE_CPIR:
		case CLOWNZ80_OPCODE_CPDR:
		case CLOWNZ80_OPCODE_INI:
		case CLOWNZ80_OPCODE_IND:
		case CLOWNZ80_OPCODE_INIR:
		case CLOWNZ80_OPCODE_INDR:
		case CLOWNZ80_OPCODE_OUTI:
		case CLOWNZ80_OPCODE_OUTD:
		case CLOWNZ80_OPCODE_OTIR:
		case CLOWNZ80_OPCODE_OTDR:
			break;
	}
}

static void PrintOperand(State* const state, const unsigned int operand_index)
{
	switch (state->metadata.operands[operand_index])
	{
		case CLOWNZ80_OPERAND_NONE:
			state->print_callback(state->user_data, "[NONE]");
			break;
		case CLOWNZ80_OPERAND_A:
			state->print_callback(state->user_data, "a");
			break;
		case CLOWNZ80_OPERAND_B:
			state->print_callback(state->user_data, "b");
			break;
		case CLOWNZ80_OPERAND_C:
			state->print_callback(state->user_data, "c");
			break;
		case CLOWNZ80_OPERAND_D:
			state->print_callback(state->user_data, "d");
			break;
		case CLOWNZ80_OPERAND_E:
			state->print_callback(state->user_data, "e");
			break;
		case CLOWNZ80_OPERAND_H:
			state->print_callback(state->user_data, "h");
			break;
		case CLOWNZ80_OPERAND_L:
			state->print_callback(state->user_data, "l");
			break;
		case CLOWNZ80_OPERAND_IXH:
			state->print_callback(state->user_data, "ixh");
			break;
		case CLOWNZ80_OPERAND_IXL:
			state->print_callback(state->user_data, "ixl");
			break;
		case CLOWNZ80_OPERAND_IYH:
			state->print_callback(state->user_data, "iyh");
			break;
		case CLOWNZ80_OPERAND_IYL:
			state->print_callback(state->user_data, "iyl");
			break;
		case CLOWNZ80_OPERAND_AF:
			state->print_callback(state->user_data, "af");
			break;
		case CLOWNZ80_OPERAND_BC:
			state->print_callback(state->user_data, "bc");
			break;
		case CLOWNZ80_OPERAND_DE:
			state->print_callback(state->user_data, "de");
			break;
		case CLOWNZ80_OPERAND_HL:
			state->print_callback(state->user_data, "hl");
			break;
		case CLOWNZ80_OPERAND_IX:
			state->print_callback(state->user_data, "ix");
			break;
		case CLOWNZ80_OPERAND_IY:
			state->print_callback(state->user_data, "iy");
			break;
		case CLOWNZ80_OPERAND_PC:
			state->print_callback(state->user_data, "pc");
			break;
		case CLOWNZ80_OPERAND_SP:
			state->print_callback(state->user_data, "sp");
			break;
		case CLOWNZ80_OPERAND_BC_INDIRECT:
			state->print_callback(state->user_data, "(bc)");
			break;
		case CLOWNZ80_OPERAND_DE_INDIRECT:
			state->print_callback(state->user_data, "(de)");
			break;
		case CLOWNZ80_OPERAND_HL_INDIRECT:
			state->print_callback(state->user_data, "(hl)");
			break;
		case CLOWNZ80_OPERAND_IX_INDIRECT:
			state->print_callback(state->user_data, "(ix%+d)", ReadSignedByte(state));
			break;
		case CLOWNZ80_OPERAND_IY_INDIRECT:
			state->print_callback(state->user_data, "(iy%+d)", ReadSignedByte(state));
			break;
		case CLOWNZ80_OPERAND_ADDRESS:
			state->print_callback(state->user_data, "(");
			PrintHexadecimal(state, ReadTwoBytes(state));
			state->print_callback(state->user_data, ")");
			break;
		case CLOWNZ80_OPERAND_LITERAL_8BIT:
			PrintHexadecimal(state, ReadByte(state));
			break;
		case CLOWNZ80_OPERAND_LITERAL_16BIT:
			PrintHexadecimal(state, ReadTwoBytes(state));
			break;
	}
}

static cc_bool DisassembleInstruction(State* const state)
{
	ClownZ80_InstructionMode instruction_mode = CLOWNZ80_INSTRUCTION_MODE_NORMAL;
	ClownZ80_RegisterMode register_mode = CLOWNZ80_REGISTER_MODE_HL;

	state->print_callback(state->user_data, "%08lX: ", state->address);

	for (;;)
	{
		ClownZ80_DecodeInstructionMetadata(&state->metadata, instruction_mode, register_mode, ReadByte(state));

		switch (state->metadata.opcode)
		{
			case CLOWNZ80_OPCODE_CB_PREFIX:
				instruction_mode = CLOWNZ80_INSTRUCTION_MODE_BITS;
				continue;
			case CLOWNZ80_OPCODE_DD_PREFIX:
				register_mode = CLOWNZ80_REGISTER_MODE_IX;
				continue;
			case CLOWNZ80_OPCODE_ED_PREFIX:
				instruction_mode = CLOWNZ80_INSTRUCTION_MODE_MISC;
				continue;
			case CLOWNZ80_OPCODE_FD_PREFIX:
				register_mode = CLOWNZ80_REGISTER_MODE_IY;
				continue;
		}

		break;
	}

	state->print_callback(state->user_data, "%-5s", GetOpcodeString((ClownZ80_Opcode)state->metadata.opcode));
	PrintSpecialOperands(state);

	if (state->metadata.operands[0] != CLOWNZ80_OPERAND_NONE && state->metadata.operands[1] != CLOWNZ80_OPERAND_NONE)
	{
		PrintOperand(state, 1);
		state->print_callback(state->user_data, ",");
		PrintOperand(state, 0);
	}
	else if (state->metadata.operands[0] != CLOWNZ80_OPERAND_NONE)
	{
		PrintOperand(state, 0);
	}
	else if (state->metadata.operands[1] != CLOWNZ80_OPERAND_NONE)
	{
		PrintOperand(state, 1);
	}

	state->print_callback(state->user_data, "\n");

	return !IsTerminatingInstruction((ClownZ80_Opcode)state->metadata.opcode);
}

void ClownZ80_Disassemble(const unsigned long address, const unsigned int maximum_instructions, const ClownZ80_ReadCallback read_callback, CC_ATTRIBUTE_PRINTF(2, 3) const ClownZ80_PrintCallback print_callback, const void* const user_data)
{
	State state;
	unsigned int i;

	state.address = address;
	state.read_callback = read_callback;
	state.print_callback = print_callback;
	state.user_data = (void*)user_data;

	for (i = 0; i < maximum_instructions; ++i)
		if (!DisassembleInstruction(&state))
			break;
}
