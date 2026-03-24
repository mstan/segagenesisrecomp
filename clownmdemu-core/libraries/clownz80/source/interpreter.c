/*
Useful:
https://floooh.github.io/2021/12/06/z80-instruction-timing.html
*/

/* TODO: R and I registers. */

#include "interpreter.h"

#include <assert.h>
#include <stddef.h>

#include "../libraries/clowncommon/clowncommon.h"

#include "common.h"

enum
{
	FLAG_BIT_CARRY = 0,
	FLAG_BIT_ADD_SUBTRACT = 1,
	FLAG_BIT_PARITY_OVERFLOW = 2,
	FLAG_BIT_HALF_CARRY = 4,
	FLAG_BIT_ZERO = 6,
	FLAG_BIT_SIGN = 7,
	FLAG_MASK_CARRY = 1 << FLAG_BIT_CARRY,
	FLAG_MASK_ADD_SUBTRACT = 1 << FLAG_BIT_ADD_SUBTRACT,
	FLAG_MASK_PARITY_OVERFLOW = 1 << FLAG_BIT_PARITY_OVERFLOW,
	FLAG_MASK_HALF_CARRY = 1 << FLAG_BIT_HALF_CARRY,
	FLAG_MASK_ZERO = 1 << FLAG_BIT_ZERO,
	FLAG_MASK_SIGN = 1 << FLAG_BIT_SIGN
};

typedef struct Z80Instruction
{
#ifdef CLOWNZ80_PRECOMPUTE_INSTRUCTION_METADATA
	const ClownZ80_InstructionMetadata *metadata;
#else
	ClownZ80_InstructionMetadata *metadata;
#endif
	cc_u16f literal;
	cc_u16f address;
	cc_bool double_prefix_mode;
} Z80Instruction;

#ifdef CLOWNZ80_PRECOMPUTE_INSTRUCTION_METADATA
static ClownZ80_InstructionMetadata instruction_metadata_lookup_normal[3][0x100];
static ClownZ80_InstructionMetadata instruction_metadata_lookup_bits[3][0x100];
static ClownZ80_InstructionMetadata instruction_metadata_lookup_misc[0x100];
#endif

static cc_bool EvaluateCondition(const cc_u8l flags, const ClownZ80_Condition condition)
{
	switch (condition)
	{
		case CLOWNZ80_CONDITION_NOT_ZERO:
			return (flags & FLAG_MASK_ZERO) == 0;

		case CLOWNZ80_CONDITION_ZERO:
			return (flags & FLAG_MASK_ZERO) != 0;

		case CLOWNZ80_CONDITION_NOT_CARRY:
			return (flags & FLAG_MASK_CARRY) == 0;

		case CLOWNZ80_CONDITION_CARRY:
			return (flags & FLAG_MASK_CARRY) != 0;

		case CLOWNZ80_CONDITION_PARITY_OVERFLOW:
			return (flags & FLAG_MASK_PARITY_OVERFLOW) == 0;

		case CLOWNZ80_CONDITION_PARITY_EQUALITY:
			return (flags & FLAG_MASK_PARITY_OVERFLOW) != 0;

		case CLOWNZ80_CONDITION_PLUS:
			return (flags & FLAG_MASK_SIGN) == 0;

		case CLOWNZ80_CONDITION_MINUS:
			return (flags & FLAG_MASK_SIGN) != 0;

		default:
			/* Should never occur. */
			assert(0);
			return cc_false;
	}
}

static cc_u16f MemoryRead(ClownZ80_State* const state, const ClownZ80_ReadAndWriteCallbacks* const callbacks, const cc_u16f address)
{
	/* Memory accesses take 3 cycles. */
	state->cycles += 3;

	return callbacks->read((void*)callbacks->user_data, address);
}

static void MemoryWrite(ClownZ80_State* const state, const ClownZ80_ReadAndWriteCallbacks* const callbacks, const cc_u16f address, const cc_u16f data)
{
	/* Memory accesses take 3 cycles. */
	state->cycles += 3;

	callbacks->write((void*)callbacks->user_data, address, data);
}

static cc_u16f InstructionMemoryRead(ClownZ80_State* const state, const ClownZ80_ReadAndWriteCallbacks* const callbacks)
{
	const cc_u16f data = MemoryRead(state, callbacks, state->program_counter);

	++state->program_counter;
	state->program_counter &= 0xFFFF;

	return data;
}

static cc_u16f OpcodeFetch(ClownZ80_State* const state, const ClownZ80_ReadAndWriteCallbacks* const callbacks)
{
	/* Opcode fetches take an extra cycle. */
	++state->cycles;

	/* Increment the lower 7 bits of the 'R' register. */
	state->r = (state->r & 0x80) | ((state->r + 1) & 0x7F);

	return InstructionMemoryRead(state, callbacks);
}

/* TODO: Should the bytes be written in reverse order? */
static cc_u16f MemoryRead16Bit(ClownZ80_State* const state, const ClownZ80_ReadAndWriteCallbacks* const callbacks, const cc_u16f address)
{
	cc_u16f value;
	value = MemoryRead(state, callbacks, address + 0);
	value |= MemoryRead(state, callbacks, (address + 1) & 0xFFFF) << 8;
	return value;
}

static void MemoryWrite16Bit(ClownZ80_State* const state, const ClownZ80_ReadAndWriteCallbacks* const callbacks, const cc_u16f address, const cc_u16f value)
{
	MemoryWrite(state, callbacks, address + 0, value & 0xFF);
	MemoryWrite(state, callbacks, (address + 1) & 0xFFFF, value >> 8);
}

static cc_u16f ReadOperand(ClownZ80_State* const state, const ClownZ80_ReadAndWriteCallbacks* const callbacks, const Z80Instruction* const instruction, ClownZ80_Operand operand)
{
	cc_u16f value;

	/* Handle double-prefix instructions. */
	/* Technically, this is only relevant to the destination operand and not the source operand,
	   but double-prefix instructions don't have a source operand, so this is not a problem. */
	if (instruction->double_prefix_mode)
		operand = state->register_mode == CLOWNZ80_REGISTER_MODE_IX ? CLOWNZ80_OPERAND_IX_INDIRECT : CLOWNZ80_OPERAND_IY_INDIRECT;

	switch (operand)
	{
		default:
		case CLOWNZ80_OPERAND_NONE:
			/* Should never happen. */
			assert(0);
			/* Fallthrough */
		case CLOWNZ80_OPERAND_A:
			value = state->a;
			break;

		case CLOWNZ80_OPERAND_B:
			value = state->b;
			break;

		case CLOWNZ80_OPERAND_C:
			value = state->c;
			break;

		case CLOWNZ80_OPERAND_D:
			value = state->d;
			break;

		case CLOWNZ80_OPERAND_E:
			value = state->e;
			break;

		case CLOWNZ80_OPERAND_H:
			value = state->h;
			break;

		case CLOWNZ80_OPERAND_L:
			value = state->l;
			break;

		case CLOWNZ80_OPERAND_IXH:
			value = state->ixh;
			break;

		case CLOWNZ80_OPERAND_IXL:
			value = state->ixl;
			break;

		case CLOWNZ80_OPERAND_IYH:
			value = state->iyh;
			break;

		case CLOWNZ80_OPERAND_IYL:
			value = state->iyl;
			break;

		case CLOWNZ80_OPERAND_AF:
			value = ((cc_u16f)state->a << 8) | state->f;
			break;

		case CLOWNZ80_OPERAND_BC:
			value = ((cc_u16f)state->b << 8) | state->c;
			break;

		case CLOWNZ80_OPERAND_DE:
			value = ((cc_u16f)state->d << 8) | state->e;
			break;

		case CLOWNZ80_OPERAND_HL:
			value = ((cc_u16f)state->h << 8) | state->l;
			break;

		case CLOWNZ80_OPERAND_IX:
			value = ((cc_u16f)state->ixh << 8) | state->ixl;
			break;

		case CLOWNZ80_OPERAND_IY:
			value = ((cc_u16f)state->iyh << 8) | state->iyl;
			break;

		case CLOWNZ80_OPERAND_PC:
			value = state->program_counter;
			break;

		case CLOWNZ80_OPERAND_SP:
			value = state->stack_pointer;
			break;

		case CLOWNZ80_OPERAND_LITERAL_8BIT:
		case CLOWNZ80_OPERAND_LITERAL_16BIT:
			value = instruction->literal;
			break;

		case CLOWNZ80_OPERAND_BC_INDIRECT:
		case CLOWNZ80_OPERAND_DE_INDIRECT:
		case CLOWNZ80_OPERAND_HL_INDIRECT:
		case CLOWNZ80_OPERAND_IX_INDIRECT:
		case CLOWNZ80_OPERAND_IY_INDIRECT:
		case CLOWNZ80_OPERAND_ADDRESS:
			value = MemoryRead(state, callbacks, instruction->address);

			if (instruction->metadata->opcode == CLOWNZ80_OPCODE_LD_16BIT)
				value |= MemoryRead(state, callbacks, instruction->address + 1) << 8;

			break;
	}

	return value;
}

static void WriteOperand(ClownZ80_State* const state, const ClownZ80_ReadAndWriteCallbacks* const callbacks, const Z80Instruction* const instruction, const ClownZ80_Operand operand, const cc_u16f value)
{
	/* Handle double-prefix instructions. */
	const ClownZ80_Operand double_prefix_operand = state->register_mode == CLOWNZ80_REGISTER_MODE_IX ? CLOWNZ80_OPERAND_IX_INDIRECT : CLOWNZ80_OPERAND_IY_INDIRECT;

	/* Don't do a redundant write in double-prefix mode when operating on '(IX+*)' or (IY+*). */
	if (instruction->double_prefix_mode && operand != double_prefix_operand)
		WriteOperand(state, callbacks, instruction, double_prefix_operand, value);

	switch (operand)
	{
		default:
		case CLOWNZ80_OPERAND_NONE:
		case CLOWNZ80_OPERAND_LITERAL_8BIT:
		case CLOWNZ80_OPERAND_LITERAL_16BIT:
			/* Should never happen. */
			assert(0);
			break;

		case CLOWNZ80_OPERAND_A:
			state->a = value;
			break;

		case CLOWNZ80_OPERAND_B:
			state->b = value;
			break;

		case CLOWNZ80_OPERAND_C:
			state->c = value;
			break;

		case CLOWNZ80_OPERAND_D:
			state->d = value;
			break;

		case CLOWNZ80_OPERAND_E:
			state->e = value;
			break;

		case CLOWNZ80_OPERAND_H:
			state->h = value;
			break;

		case CLOWNZ80_OPERAND_L:
			state->l = value;
			break;

		case CLOWNZ80_OPERAND_IXH:
			state->ixh = value;
			break;

		case CLOWNZ80_OPERAND_IXL:
			state->ixl = value;
			break;

		case CLOWNZ80_OPERAND_IYH:
			state->iyh = value;
			break;

		case CLOWNZ80_OPERAND_IYL:
			state->iyl = value;
			break;

		case CLOWNZ80_OPERAND_AF:
			state->a = value >> 8;
			state->f = value & 0xFF;
			break;

		case CLOWNZ80_OPERAND_BC:
			state->b = value >> 8;
			state->c = value & 0xFF;
			break;

		case CLOWNZ80_OPERAND_DE:
			state->d = value >> 8;
			state->e = value & 0xFF;
			break;

		case CLOWNZ80_OPERAND_HL:
			state->h = value >> 8;
			state->l = value & 0xFF;
			break;

		case CLOWNZ80_OPERAND_IX:
			state->ixh = value >> 8;
			state->ixl = value & 0xFF;
			break;

		case CLOWNZ80_OPERAND_IY:
			state->iyh = value >> 8;
			state->iyl = value & 0xFF;
			break;

		case CLOWNZ80_OPERAND_PC:
			state->program_counter = value;
			break;

		case CLOWNZ80_OPERAND_SP:
			state->stack_pointer = value;
			break;

		case CLOWNZ80_OPERAND_BC_INDIRECT:
		case CLOWNZ80_OPERAND_DE_INDIRECT:
		case CLOWNZ80_OPERAND_HL_INDIRECT:
		case CLOWNZ80_OPERAND_IX_INDIRECT:
		case CLOWNZ80_OPERAND_IY_INDIRECT:
		case CLOWNZ80_OPERAND_ADDRESS:
			if (instruction->metadata->opcode == CLOWNZ80_OPCODE_LD_16BIT)
				MemoryWrite16Bit(state, callbacks, instruction->address, value);
			else
				MemoryWrite(state, callbacks, instruction->address, value);

			break;
	}
}

static void DecodeInstruction(ClownZ80_State* const state, const ClownZ80_ReadAndWriteCallbacks* const callbacks, Z80Instruction* const instruction)
{
	cc_u16f opcode;
	cc_u16f displacement;
	cc_u16f i;

	opcode = OpcodeFetch(state, callbacks);

	/* Shut up a 'may be used uninitialised' compiler warning. */
	displacement = 0;

#ifdef CLOWNZ80_PRECOMPUTE_INSTRUCTION_METADATA
	instruction->metadata = &instruction_metadata_lookup_normal[state->register_mode][opcode];
#else
	Z80_DecodeInstructionMetadata(instruction->metadata, CLOWNZ80_INSTRUCTION_MODE_NORMAL, (ClownZ80_RegisterMode)state->register_mode, opcode);
#endif

	/* Obtain displacement byte if one exists. */
	if (instruction->metadata->has_displacement)
	{
		displacement = InstructionMemoryRead(state, callbacks);
		displacement = CC_SIGN_EXTEND_UINT(7, displacement);

		/* The displacement byte adds 5 cycles on top of the 3 required to read it. */
		state->cycles += 5;
	}

	instruction->double_prefix_mode = cc_false;

	/* Handle prefix instructions. */
	switch ((ClownZ80_Opcode)instruction->metadata->opcode)
	{
		default:
			/* Nothing to do here. */
			break;

		case CLOWNZ80_OPCODE_CB_PREFIX:
			if (state->register_mode == CLOWNZ80_REGISTER_MODE_HL)
			{
				/* Normal mode. */
				opcode = OpcodeFetch(state, callbacks);
				
			#ifdef CLOWNZ80_PRECOMPUTE_INSTRUCTION_METADATA
				instruction->metadata = &instruction_metadata_lookup_bits[state->register_mode][opcode];
			#else
				ClownZ80_DecodeInstructionMetadata(instruction->metadata, CLOWNZ80_INSTRUCTION_MODE_BITS, (ClownZ80_RegisterMode)state->register_mode, opcode);
			#endif
			}
			else
			{
				/* Double-prefix mode. */
				instruction->double_prefix_mode = cc_true;

				/* Yes, double-prefix mode fetches the opcode with a regular memory read. */
				opcode = InstructionMemoryRead(state, callbacks);

				/* Reading the opcode is overlaid with the 5 displacement cycles, so the above memory read doesn't cost 3 cycles. */
				state->cycles -= 3;

				if (state->register_mode == CLOWNZ80_REGISTER_MODE_IX)
					instruction->address = ((((cc_u16f)state->ixh << 8) | state->ixl) + displacement) & 0xFFFF;
				else /*if (state->register_mode == CLOWNZ80_REGISTER_MODE_IY)*/
					instruction->address = ((((cc_u16f)state->iyh << 8) | state->iyl) + displacement) & 0xFFFF;

				/* TODO: Use a separate lookup for double-prefix mode? */
			#ifdef CLOWNZ80_PRECOMPUTE_INSTRUCTION_METADATA
				instruction->metadata = &instruction_metadata_lookup_bits[CLOWNZ80_REGISTER_MODE_HL][opcode];
			#else
				ClownZ80_DecodeInstructionMetadata(instruction->metadata, CLOWNZ80_INSTRUCTION_MODE_BITS, CLOWNZ80_REGISTER_MODE_HL, opcode);
			#endif

				if (instruction->metadata->operands[1] == CLOWNZ80_OPERAND_HL_INDIRECT)
				#ifdef CLOWNZ80_PRECOMPUTE_INSTRUCTION_METADATA
					instruction->metadata = &instruction_metadata_lookup_bits[state->register_mode][opcode];
				#else
					ClownZ80_DecodeInstructionMetadata(instruction->metadata, CLOWNZ80_INSTRUCTION_MODE_BITS, (ClownZ80_RegisterMode)state->register_mode, opcode);
				#endif
			}

			break;

		case CLOWNZ80_OPCODE_ED_PREFIX:
			opcode = OpcodeFetch(state, callbacks);

		#ifdef CLOWNZ80_PRECOMPUTE_INSTRUCTION_METADATA
			instruction->metadata = &instruction_metadata_lookup_misc[opcode];
		#else
			ClownZ80_DecodeInstructionMetadata(instruction->metadata, CLOWNZ80_INSTRUCTION_MODE_MISC, CLOWNZ80_REGISTER_MODE_HL, opcode);
		#endif

			break;
	}

	/* Obtain literal data. */
	switch ((ClownZ80_Operand)instruction->metadata->operands[0])
	{
		default:
			/* Nothing to do here. */
			break;

		case CLOWNZ80_OPERAND_LITERAL_8BIT:
			instruction->literal = InstructionMemoryRead(state, callbacks);

			if (instruction->metadata->has_displacement)
			{
				/* Reading the literal is overlaid with the 5 displacement cycles, so the above memory read doesn't cost 3 cycles. */
				state->cycles -= 3;
			}

			break;

		case CLOWNZ80_OPERAND_LITERAL_16BIT:
			instruction->literal = InstructionMemoryRead(state, callbacks);
			instruction->literal |= InstructionMemoryRead(state, callbacks) << 8;
			break;
	}

	/* Pre-calculate the address of indirect memory operands. */
	for (i = 0; i < 2; ++i)
	{
		switch ((ClownZ80_Operand)instruction->metadata->operands[i])
		{
			default:
				break;

			case CLOWNZ80_OPERAND_BC_INDIRECT:
				instruction->address = ((cc_u16f)state->b << 8) | state->c;
				break;

			case CLOWNZ80_OPERAND_DE_INDIRECT:
				instruction->address = ((cc_u16f)state->d << 8) | state->e;
				break;

			case CLOWNZ80_OPERAND_HL_INDIRECT:
				instruction->address = ((cc_u16f)state->h << 8) | state->l;
				break;

			case CLOWNZ80_OPERAND_IX_INDIRECT:
				instruction->address = ((((cc_u16f)state->ixh << 8) | state->ixl) + displacement) & 0xFFFF;
				break;

			case CLOWNZ80_OPERAND_IY_INDIRECT:
				instruction->address = ((((cc_u16f)state->iyh << 8) | state->iyl) + displacement) & 0xFFFF;
				break;

			case CLOWNZ80_OPERAND_ADDRESS:
				instruction->address = InstructionMemoryRead(state, callbacks);
				instruction->address |= InstructionMemoryRead(state, callbacks) << 8;
				break;
		}
	}
}

static cc_bool ComputeParity(cc_u8f value)
{
	value ^= value >> 4;
	value ^= value >> 2;
	value ^= value >> 1;

	return (value & 1) == 0;
}

#define SWAP(a, b) \
	swap_holder = a; \
	a = b; \
	b = swap_holder

#define CONDITION_SIGN_BASE(bit) state->f |= (result_value >> (bit - FLAG_BIT_SIGN)) & FLAG_MASK_SIGN
#define CONDITION_CARRY_BASE(variable, bit) state->f |= (variable >> (bit - FLAG_BIT_CARRY)) & FLAG_MASK_CARRY
#define CONDITION_HALF_CARRY_BASE(bit) state->f |= ((source_value ^ destination_value ^ result_value) >> (bit - FLAG_BIT_HALF_CARRY)) & FLAG_MASK_HALF_CARRY
#define CONDITION_OVERFLOW_BASE(bit) state->f |= ((~(source_value ^ destination_value) & (source_value ^ result_value)) >> (bit - FLAG_BIT_PARITY_OVERFLOW)) & FLAG_MASK_PARITY_OVERFLOW

#define CONDITION_SIGN_16BIT CONDITION_SIGN_BASE(15)
#define CONDITION_HALF_CARRY_16BIT CONDITION_HALF_CARRY_BASE(12)
#define CONDITION_OVERFLOW_16BIT CONDITION_OVERFLOW_BASE(15)
#define CONDITION_CARRY_16BIT CONDITION_CARRY_BASE(result_value_with_carry_16bit, 16)

#define CONDITION_SIGN CONDITION_SIGN_BASE(7)
#define CONDITION_ZERO state->f |= result_value == 0 ? FLAG_MASK_ZERO : 0
#define CONDITION_HALF_CARRY CONDITION_HALF_CARRY_BASE(4)
#define CONDITION_OVERFLOW CONDITION_OVERFLOW_BASE(7)
#define CONDITION_PARITY state->f |= ComputeParity(result_value) ? FLAG_MASK_PARITY_OVERFLOW : 0
#define CONDITION_CARRY CONDITION_CARRY_BASE(result_value_with_carry, 8)

#define READ_SOURCE source_value = ReadOperand(state, callbacks, instruction, (ClownZ80_Operand)instruction->metadata->operands[0])
#define READ_DESTINATION destination_value = ReadOperand(state, callbacks, instruction, (ClownZ80_Operand)instruction->metadata->operands[1])

#define WRITE_DESTINATION WriteOperand(state, callbacks, instruction, (ClownZ80_Operand)instruction->metadata->operands[1], result_value)

static void ExecuteInstruction(ClownZ80_State* const state, const ClownZ80_ReadAndWriteCallbacks* const callbacks, const Z80Instruction* const instruction)
{
	cc_u16f source_value;
	cc_u16f destination_value;
	cc_u16f result_value;
	cc_u16f result_value_with_carry;
	cc_u32f result_value_with_carry_16bit;
	cc_u8l swap_holder;
	cc_bool carry;

	state->register_mode = CLOWNZ80_REGISTER_MODE_HL;

	switch ((ClownZ80_Opcode)instruction->metadata->opcode)
	{
		#define UNIMPLEMENTED_Z80_INSTRUCTION(instruction) callbacks->log((void*)callbacks->user_data, "Unimplemented instruction " instruction " used at 0x%" CC_PRIXLEAST16, state->program_counter)

		case CLOWNZ80_OPCODE_NOP:
			/* Does nothing, naturally. */
			break;

		case CLOWNZ80_OPCODE_EX_AF_AF:
			SWAP(state->a, state->a_);
			SWAP(state->f, state->f_);
			break;

		case CLOWNZ80_OPCODE_DJNZ:
			/* This instruction takes an extra cycle. */
			state->cycles += 1;

			--state->b;
			state->b &= 0xFF;

			if (state->b != 0)
			{
				state->program_counter += CC_SIGN_EXTEND_UINT(7, instruction->literal);
				state->program_counter &= 0xFFFF;

				/* Branching takes 5 cycles. */
				state->cycles += 5;
			}

			break;

		case CLOWNZ80_OPCODE_JR_CONDITIONAL:
			if (!EvaluateCondition(state->f, (ClownZ80_Condition)instruction->metadata->condition))
				break;
			/* Fallthrough */
		case CLOWNZ80_OPCODE_JR_UNCONDITIONAL:
			state->program_counter += CC_SIGN_EXTEND_UINT(7, instruction->literal);
			state->program_counter &= 0xFFFF;

			/* Branching takes 5 cycles. */
			state->cycles += 5;

			break;

		case CLOWNZ80_OPCODE_LD_8BIT:
		case CLOWNZ80_OPCODE_LD_16BIT:
			READ_SOURCE;

			result_value = source_value;

			WRITE_DESTINATION;

			break;

		case CLOWNZ80_OPCODE_ADD_HL:
			READ_SOURCE;
			READ_DESTINATION;

			result_value_with_carry_16bit = (cc_u32f)source_value + (cc_u32f)destination_value;
			result_value = result_value_with_carry_16bit & 0xFFFF;

			state->f &= FLAG_MASK_SIGN | FLAG_MASK_ZERO | FLAG_MASK_PARITY_OVERFLOW;

			CONDITION_CARRY_16BIT;
			CONDITION_HALF_CARRY_16BIT;

			WRITE_DESTINATION;

			/* This instruction requires an extra 7 cycles. */
			state->cycles += 7;

			break;

		case CLOWNZ80_OPCODE_INC_16BIT:
			READ_DESTINATION;

			result_value = (destination_value + 1) & 0xFFFF;

			WRITE_DESTINATION;

			/* This instruction requires an extra 2 cycles. */
			state->cycles += 2;

			break;

		case CLOWNZ80_OPCODE_DEC_16BIT:
			READ_DESTINATION;

			result_value = (destination_value - 1) & 0xFFFF;

			WRITE_DESTINATION;

			/* This instruction requires an extra 2 cycles. */
			state->cycles += 2;

			break;

		case CLOWNZ80_OPCODE_INC_8BIT:
			source_value = 1;
			READ_DESTINATION;

			result_value = (destination_value + source_value) & 0xFF;

			state->f &= FLAG_MASK_CARRY;

			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_HALF_CARRY;
			CONDITION_OVERFLOW;

			WRITE_DESTINATION;

			/* The memory-accessing version takes an extra cycle. */
			state->cycles += instruction->metadata->operands[1] == CLOWNZ80_OPERAND_HL_INDIRECT
				|| instruction->metadata->operands[1] == CLOWNZ80_OPERAND_IX_INDIRECT
				|| instruction->metadata->operands[1] == CLOWNZ80_OPERAND_IY_INDIRECT;

			break;

		case CLOWNZ80_OPCODE_DEC_8BIT:
			source_value = -1;
			READ_DESTINATION;

			result_value = (destination_value + source_value) & 0xFF;

			state->f &= FLAG_MASK_CARRY;

			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_HALF_CARRY;
			CONDITION_OVERFLOW;

			state->f ^= FLAG_MASK_HALF_CARRY;
			state->f |= FLAG_MASK_ADD_SUBTRACT;

			WRITE_DESTINATION;

			/* The memory-accessing version takes an extra cycle. */
			state->cycles += instruction->metadata->operands[1] == CLOWNZ80_OPERAND_HL_INDIRECT
				|| instruction->metadata->operands[1] == CLOWNZ80_OPERAND_IX_INDIRECT
				|| instruction->metadata->operands[1] == CLOWNZ80_OPERAND_IY_INDIRECT;

			break;

		case CLOWNZ80_OPCODE_RLCA:
			carry = (state->a & 0x80) != 0;

			state->a <<= 1;
			state->a &= 0xFF;
			state->a |= carry ? 0x01 : 0;

			state->f &= FLAG_MASK_SIGN | FLAG_MASK_ZERO | FLAG_MASK_PARITY_OVERFLOW;
			state->f |= carry ? FLAG_MASK_CARRY : 0;

			break;

		case CLOWNZ80_OPCODE_RRCA:
			carry = (state->a & 0x01) != 0;

			state->a >>= 1;
			state->a |= carry ? 0x80 : 0;

			state->f &= FLAG_MASK_SIGN | FLAG_MASK_ZERO | FLAG_MASK_PARITY_OVERFLOW;
			state->f |= carry ? FLAG_MASK_CARRY : 0;

			break;

		case CLOWNZ80_OPCODE_RLA:
			carry = (state->a & 0x80) != 0;

			state->a <<= 1;
			state->a &= 0xFF;
			state->a |= (state->f & FLAG_MASK_CARRY) != 0 ? 1 : 0;

			state->f &= FLAG_MASK_SIGN | FLAG_MASK_ZERO | FLAG_MASK_PARITY_OVERFLOW;
			state->f |= carry ? FLAG_MASK_CARRY : 0;

			break;

		case CLOWNZ80_OPCODE_RRA:
			carry = (state->a & 0x01) != 0;

			state->a >>= 1;
			state->a |= (state->f & FLAG_MASK_CARRY) != 0 ? 0x80 : 0;

			state->f &= FLAG_MASK_SIGN | FLAG_MASK_ZERO | FLAG_MASK_PARITY_OVERFLOW;
			state->f |= carry ? FLAG_MASK_CARRY : 0;

			break;

		case CLOWNZ80_OPCODE_DAA:
		{
			cc_u16f correction_factor;

			const cc_u16f original_a = state->a;

			correction_factor = ((state->a + 0x66) ^ state->a) & 0x110;
			correction_factor |= (state->f & FLAG_MASK_CARRY) << (8 - FLAG_BIT_CARRY);
			correction_factor |= (state->f & FLAG_MASK_HALF_CARRY) << (4 - FLAG_BIT_HALF_CARRY);
			correction_factor = (correction_factor >> 2) | (correction_factor >> 3);

			if ((state->f & FLAG_MASK_ADD_SUBTRACT) != 0)
				state->a -= correction_factor;
			else
				state->a += correction_factor;

			state->a &= 0xFF;

			state->f &= FLAG_MASK_ADD_SUBTRACT;
			state->f |= (state->a >> (7 - FLAG_BIT_SIGN)) & FLAG_MASK_SIGN;
			state->f |= (state->a == 0) << FLAG_BIT_ZERO;
			state->f |= ((original_a ^ state->a) >> (4 - FLAG_BIT_HALF_CARRY)) & FLAG_MASK_HALF_CARRY; /* Binary carry. */
			state->f |= ComputeParity(state->a) ? FLAG_MASK_PARITY_OVERFLOW : 0;
			state->f |= (correction_factor >> (6 - FLAG_BIT_CARRY)) & FLAG_MASK_CARRY; /* Decimal carry. */

			break;
		}

		case CLOWNZ80_OPCODE_CPL:
			state->a = ~state->a;
			state->a &= 0xFF;

			state->f |= FLAG_MASK_HALF_CARRY | FLAG_MASK_ADD_SUBTRACT;

			break;

		case CLOWNZ80_OPCODE_SCF:
			state->f &= FLAG_MASK_SIGN | FLAG_MASK_ZERO | FLAG_MASK_PARITY_OVERFLOW;
			state->f |= FLAG_MASK_CARRY;
			break;

		case CLOWNZ80_OPCODE_CCF:
			state->f &= ~(FLAG_MASK_ADD_SUBTRACT | FLAG_MASK_HALF_CARRY);

			state->f |= (state->f & FLAG_MASK_CARRY) != 0 ? FLAG_MASK_HALF_CARRY : 0;
			state->f ^= FLAG_MASK_CARRY;

			break;

		case CLOWNZ80_OPCODE_HALT:
			/* TODO */
			UNIMPLEMENTED_Z80_INSTRUCTION("HALT");
			break;

		case CLOWNZ80_OPCODE_ADD_A:
			READ_SOURCE;
			destination_value = state->a;

			result_value_with_carry = destination_value + source_value;
			result_value = result_value_with_carry & 0xFF;

			state->f = 0;
			CONDITION_CARRY;
			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_HALF_CARRY;
			CONDITION_OVERFLOW;

			state->a = result_value;

			break;

		case CLOWNZ80_OPCODE_ADC_A:
			READ_SOURCE;
			destination_value = state->a;

			result_value_with_carry = destination_value + source_value + ((state->f & FLAG_MASK_CARRY) != 0 ? 1 : 0);
			result_value = result_value_with_carry & 0xFF;

			state->f = 0;
			CONDITION_CARRY;
			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_HALF_CARRY;
			CONDITION_OVERFLOW;

			state->a = result_value;

			break;

		case CLOWNZ80_OPCODE_SUB:
			READ_SOURCE;
			source_value = ~source_value;
			destination_value = state->a;

			result_value_with_carry = destination_value + source_value + 1;
			result_value = result_value_with_carry & 0xFF;

			state->f = 0;
			CONDITION_CARRY;
			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_HALF_CARRY;
			CONDITION_OVERFLOW;

			state->f ^= FLAG_MASK_HALF_CARRY;
			state->f |= FLAG_MASK_ADD_SUBTRACT;

			state->a = result_value;

			break;

		case CLOWNZ80_OPCODE_SBC_A:
			READ_SOURCE;
			source_value = ~source_value;
			destination_value = state->a;

			result_value_with_carry = destination_value + source_value + ((state->f & FLAG_MASK_CARRY) != 0 ? 0 : 1);
			result_value = result_value_with_carry & 0xFF;

			state->f = 0;
			CONDITION_CARRY;
			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_HALF_CARRY;
			CONDITION_OVERFLOW;

			state->f ^= FLAG_MASK_HALF_CARRY;
			state->f |= FLAG_MASK_ADD_SUBTRACT;

			state->a = result_value;

			break;

		case CLOWNZ80_OPCODE_AND:
			READ_SOURCE;
			destination_value = state->a;

			result_value = destination_value & source_value;

			state->f = 0;
			CONDITION_SIGN;
			CONDITION_ZERO;
			state->f |= FLAG_MASK_HALF_CARRY;
			CONDITION_PARITY;

			state->a = result_value;

			break;

		case CLOWNZ80_OPCODE_XOR:
			READ_SOURCE;
			destination_value = state->a;

			result_value = destination_value ^ source_value;

			state->f = 0;
			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_PARITY;

			state->a = result_value;

			break;

		case CLOWNZ80_OPCODE_OR:
			READ_SOURCE;
			destination_value = state->a;

			result_value = destination_value | source_value;

			state->f = 0;
			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_PARITY;

			state->a = result_value;

			break;

		case CLOWNZ80_OPCODE_CP:
			READ_SOURCE;
			source_value = ~source_value;
			destination_value = state->a;

			result_value_with_carry = destination_value + source_value + 1;
			result_value = result_value_with_carry & 0xFF;

			state->f = 0;
			CONDITION_CARRY;
			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_HALF_CARRY;
			CONDITION_OVERFLOW;

			state->f ^= FLAG_MASK_HALF_CARRY;
			state->f |= FLAG_MASK_ADD_SUBTRACT;

			break;

		case CLOWNZ80_OPCODE_POP:
			result_value = MemoryRead16Bit(state, callbacks, state->stack_pointer);

			WRITE_DESTINATION;

			state->stack_pointer += 2;
			state->stack_pointer &= 0xFFFF;

			break;

		case CLOWNZ80_OPCODE_RET_CONDITIONAL:
			/* This instruction requires an extra cycle. */
			state->cycles += 1;

			if (!EvaluateCondition(state->f, (ClownZ80_Condition)instruction->metadata->condition))
				break;
			/* Fallthrough */
		case CLOWNZ80_OPCODE_RET_UNCONDITIONAL:
		case CLOWNZ80_OPCODE_RETN: /* TODO: The IFF1/IFF2 stuff. */
		case CLOWNZ80_OPCODE_RETI: /* Ditto. */
			state->program_counter = MemoryRead16Bit(state, callbacks, state->stack_pointer);
			state->stack_pointer += 2;
			state->stack_pointer &= 0xFFFF;
			break;

		case CLOWNZ80_OPCODE_EXX:
			SWAP(state->b, state->b_);
			SWAP(state->c, state->c_);
			SWAP(state->d, state->d_);
			SWAP(state->e, state->e_);
			SWAP(state->h, state->h_);
			SWAP(state->l, state->l_);
			break;

		case CLOWNZ80_OPCODE_LD_SP_HL:
			/* This instruction requires 2 cycles. */
			state->cycles += 2;

			READ_SOURCE;

			state->stack_pointer = source_value;

			break;

		case CLOWNZ80_OPCODE_JP_CONDITIONAL:
			if (!EvaluateCondition(state->f, (ClownZ80_Condition)instruction->metadata->condition))
				break;
			/* Fallthrough */
		case CLOWNZ80_OPCODE_JP_UNCONDITIONAL:
		case CLOWNZ80_OPCODE_JP_HL:
			READ_SOURCE;

			state->program_counter = source_value;

			break;

		case CLOWNZ80_OPCODE_CB_PREFIX:
		case CLOWNZ80_OPCODE_ED_PREFIX:
			/* Should never occur: these are handled by `DecodeInstruction`. */
			assert(0);
			break;

		case CLOWNZ80_OPCODE_DD_PREFIX:
			state->register_mode = CLOWNZ80_REGISTER_MODE_IX;
			break;

		case CLOWNZ80_OPCODE_FD_PREFIX:
			state->register_mode = CLOWNZ80_REGISTER_MODE_IY;
			break;

		case CLOWNZ80_OPCODE_OUT:
			/* TODO */
			UNIMPLEMENTED_Z80_INSTRUCTION("OUT");
			break;

		case CLOWNZ80_OPCODE_IN:
			/* TODO */
			UNIMPLEMENTED_Z80_INSTRUCTION("IN");
			break;

		case CLOWNZ80_OPCODE_EX_SP_HL:
			/* This instruction requires 3 extra cycles. */
			state->cycles += 3;

			READ_DESTINATION;
			result_value = MemoryRead16Bit(state, callbacks, state->stack_pointer);
			MemoryWrite16Bit(state, callbacks, state->stack_pointer, destination_value);
			WRITE_DESTINATION;
			break;

		case CLOWNZ80_OPCODE_EX_DE_HL:
			SWAP(state->d, state->h);
			SWAP(state->e, state->l);
			break;

		case CLOWNZ80_OPCODE_DI:
			state->interrupts_enabled = cc_false;
			break;

		case CLOWNZ80_OPCODE_EI:
			state->interrupts_enabled = cc_true;
			break;

		case CLOWNZ80_OPCODE_PUSH:
			/* This instruction requires an extra cycle. */
			state->cycles += 1;

			READ_SOURCE;

			--state->stack_pointer;
			state->stack_pointer &= 0xFFFF;
			MemoryWrite(state, callbacks, state->stack_pointer, source_value >> 8);

			--state->stack_pointer;
			state->stack_pointer &= 0xFFFF;
			MemoryWrite(state, callbacks, state->stack_pointer, source_value & 0xFF);

			break;

		case CLOWNZ80_OPCODE_CALL_CONDITIONAL:
			if (!EvaluateCondition(state->f, (ClownZ80_Condition)instruction->metadata->condition))
				break;
			/* Fallthrough */
		case CLOWNZ80_OPCODE_CALL_UNCONDITIONAL:
			/* This instruction takes an extra cycle. */
			state->cycles += 1;

			--state->stack_pointer;
			state->stack_pointer &= 0xFFFF;
			MemoryWrite(state, callbacks, state->stack_pointer, state->program_counter >> 8);

			--state->stack_pointer;
			state->stack_pointer &= 0xFFFF;
			MemoryWrite(state, callbacks, state->stack_pointer, state->program_counter & 0xFF);

			state->program_counter = instruction->literal;
			break;

		case CLOWNZ80_OPCODE_RST:
			/* This instruction requires an extra cycle. */
			state->cycles += 1;

			--state->stack_pointer;
			state->stack_pointer &= 0xFFFF;
			MemoryWrite(state, callbacks, state->stack_pointer, state->program_counter >> 8);

			--state->stack_pointer;
			state->stack_pointer &= 0xFFFF;
			MemoryWrite(state, callbacks, state->stack_pointer, state->program_counter & 0xFF);

			state->program_counter = instruction->metadata->embedded_literal;
			break;

		case CLOWNZ80_OPCODE_RLC:
			READ_DESTINATION;

			carry = (destination_value & 0x80) != 0;

			result_value = (destination_value << 1) & 0xFF;
			result_value |= carry ? 0x01 : 0;

			state->f = 0;
			state->f |= carry ? FLAG_MASK_CARRY : 0;
			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_PARITY;

			WRITE_DESTINATION;

			/* The memory-accessing version takes an extra cycle. */
			state->cycles += instruction->metadata->operands[1] == CLOWNZ80_OPERAND_HL_INDIRECT
				|| instruction->metadata->operands[1] == CLOWNZ80_OPERAND_IX_INDIRECT
				|| instruction->metadata->operands[1] == CLOWNZ80_OPERAND_IY_INDIRECT;

			break;

		case CLOWNZ80_OPCODE_RRC:
			READ_DESTINATION;

			carry = (destination_value & 0x01) != 0;

			result_value = destination_value >> 1;
			result_value |= carry ? 0x80 : 0;

			state->f = 0;
			state->f |= carry ? FLAG_MASK_CARRY : 0;
			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_PARITY;

			WRITE_DESTINATION;

			/* The memory-accessing version takes an extra cycle. */
			state->cycles += instruction->metadata->operands[1] == CLOWNZ80_OPERAND_HL_INDIRECT
				|| instruction->metadata->operands[1] == CLOWNZ80_OPERAND_IX_INDIRECT
				|| instruction->metadata->operands[1] == CLOWNZ80_OPERAND_IY_INDIRECT;

			break;

		case CLOWNZ80_OPCODE_RL:
			READ_DESTINATION;

			carry = (destination_value & 0x80) != 0;

			result_value = (destination_value << 1) & 0xFF;
			result_value |= (state->f &= FLAG_MASK_CARRY) != 0 ? 0x01 : 0;

			state->f = 0;
			state->f |= carry ? FLAG_MASK_CARRY : 0;
			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_PARITY;

			WRITE_DESTINATION;

			/* The memory-accessing version takes an extra cycle. */
			state->cycles += instruction->metadata->operands[1] == CLOWNZ80_OPERAND_HL_INDIRECT
				|| instruction->metadata->operands[1] == CLOWNZ80_OPERAND_IX_INDIRECT
				|| instruction->metadata->operands[1] == CLOWNZ80_OPERAND_IY_INDIRECT;

			break;

		case CLOWNZ80_OPCODE_RR:
			READ_DESTINATION;

			carry = (destination_value & 0x01) != 0;

			result_value = destination_value >> 1;
			result_value |= (state->f &= FLAG_MASK_CARRY) != 0 ? 0x80 : 0;

			state->f = 0;
			state->f |= carry ? FLAG_MASK_CARRY : 0;
			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_PARITY;

			WRITE_DESTINATION;

			/* The memory-accessing version takes an extra cycle. */
			state->cycles += instruction->metadata->operands[1] == CLOWNZ80_OPERAND_HL_INDIRECT
				|| instruction->metadata->operands[1] == CLOWNZ80_OPERAND_IX_INDIRECT
				|| instruction->metadata->operands[1] == CLOWNZ80_OPERAND_IY_INDIRECT;

			break;

		case CLOWNZ80_OPCODE_SLA:
			READ_DESTINATION;

			carry = (destination_value & 0x80) != 0;

			result_value = (destination_value << 1) & 0xFF;

			state->f = 0;
			state->f |= (result_value & 0x80) != 0 ? FLAG_MASK_SIGN : 0;
			state->f |= result_value == 0 ? FLAG_MASK_ZERO : 0;
			CONDITION_PARITY;
			state->f |= carry ? FLAG_MASK_CARRY : 0;

			WRITE_DESTINATION;

			/* The memory-accessing version takes an extra cycle. */
			state->cycles += instruction->metadata->operands[1] == CLOWNZ80_OPERAND_HL_INDIRECT
				|| instruction->metadata->operands[1] == CLOWNZ80_OPERAND_IX_INDIRECT
				|| instruction->metadata->operands[1] == CLOWNZ80_OPERAND_IY_INDIRECT;

			break;

		case CLOWNZ80_OPCODE_SLL:
			READ_DESTINATION;

			carry = (destination_value & 0x80) != 0;

			result_value = ((destination_value << 1) | 1) & 0xFF;

			state->f = 0;
			state->f |= (result_value & 0x80) != 0 ? FLAG_MASK_SIGN : 0;
			state->f |= result_value == 0 ? FLAG_MASK_ZERO : 0;
			CONDITION_PARITY;
			state->f |= carry ? FLAG_MASK_CARRY : 0;

			WRITE_DESTINATION;

			/* The memory-accessing version takes an extra cycle. */
			state->cycles += instruction->metadata->operands[1] == CLOWNZ80_OPERAND_HL_INDIRECT
				|| instruction->metadata->operands[1] == CLOWNZ80_OPERAND_IX_INDIRECT
				|| instruction->metadata->operands[1] == CLOWNZ80_OPERAND_IY_INDIRECT;

			break;

		case CLOWNZ80_OPCODE_SRA:
			READ_DESTINATION;

			carry = (destination_value & 0x01) != 0;

			result_value = (destination_value >> 1) | (destination_value & 0x80);

			state->f = 0;
			state->f |= (result_value & 0x80) != 0 ? FLAG_MASK_SIGN : 0;
			state->f |= result_value == 0 ? FLAG_MASK_ZERO : 0;
			CONDITION_PARITY;
			state->f |= carry ? FLAG_MASK_CARRY : 0;

			WRITE_DESTINATION;

			/* The memory-accessing version takes an extra cycle. */
			state->cycles += instruction->metadata->operands[1] == CLOWNZ80_OPERAND_HL_INDIRECT
				|| instruction->metadata->operands[1] == CLOWNZ80_OPERAND_IX_INDIRECT
				|| instruction->metadata->operands[1] == CLOWNZ80_OPERAND_IY_INDIRECT;

			break;

		case CLOWNZ80_OPCODE_SRL:
			READ_DESTINATION;

			carry = (destination_value & 0x01) != 0;

			result_value = destination_value >> 1;

			state->f = 0;
			state->f |= (result_value & 0x80) != 0 ? FLAG_MASK_SIGN : 0;
			state->f |= result_value == 0 ? FLAG_MASK_ZERO : 0;
			CONDITION_PARITY;
			state->f |= carry ? FLAG_MASK_CARRY : 0;

			WRITE_DESTINATION;

			/* The memory-accessing version takes an extra cycle. */
			state->cycles += instruction->metadata->operands[1] == CLOWNZ80_OPERAND_HL_INDIRECT
				|| instruction->metadata->operands[1] == CLOWNZ80_OPERAND_IX_INDIRECT
				|| instruction->metadata->operands[1] == CLOWNZ80_OPERAND_IY_INDIRECT;

			break;

		case CLOWNZ80_OPCODE_BIT:
			READ_DESTINATION;

			/* The setting of the parity and sign bits doesn't seem to be documented anywhere. */
			/* TODO: See if emulating this instruction with a SUB instruction produces the proper condition codes. */
			state->f &= FLAG_MASK_CARRY;
			state->f |= ((destination_value & instruction->metadata->embedded_literal) == 0) ? FLAG_MASK_ZERO | FLAG_MASK_PARITY_OVERFLOW : 0;
			state->f |= FLAG_MASK_HALF_CARRY;
			state->f |= instruction->metadata->embedded_literal == 0x80 && (state->f & FLAG_MASK_ZERO) == 0 ? FLAG_MASK_SIGN : 0;

			/* The memory-accessing version takes an extra cycle. */
			state->cycles += instruction->metadata->operands[1] == CLOWNZ80_OPERAND_HL_INDIRECT
				|| instruction->metadata->operands[1] == CLOWNZ80_OPERAND_IX_INDIRECT
				|| instruction->metadata->operands[1] == CLOWNZ80_OPERAND_IY_INDIRECT;

			break;

		case CLOWNZ80_OPCODE_RES:
			READ_DESTINATION;

			result_value = destination_value & instruction->metadata->embedded_literal;

			WRITE_DESTINATION;

			/* The memory-accessing version takes an extra cycle. */
			state->cycles += instruction->metadata->operands[1] == CLOWNZ80_OPERAND_HL_INDIRECT
				|| instruction->metadata->operands[1] == CLOWNZ80_OPERAND_IX_INDIRECT
				|| instruction->metadata->operands[1] == CLOWNZ80_OPERAND_IY_INDIRECT;

			break;

		case CLOWNZ80_OPCODE_SET:
			READ_DESTINATION;

			result_value = destination_value | instruction->metadata->embedded_literal;

			WRITE_DESTINATION;

			/* The memory-accessing version takes an extra cycle. */
			state->cycles += instruction->metadata->operands[1] == CLOWNZ80_OPERAND_HL_INDIRECT
				|| instruction->metadata->operands[1] == CLOWNZ80_OPERAND_IX_INDIRECT
				|| instruction->metadata->operands[1] == CLOWNZ80_OPERAND_IY_INDIRECT;

			break;

		case CLOWNZ80_OPCODE_IN_REGISTER:
			UNIMPLEMENTED_Z80_INSTRUCTION("IN (register)");
			break;

		case CLOWNZ80_OPCODE_IN_NO_REGISTER:
			UNIMPLEMENTED_Z80_INSTRUCTION("IN (no register)");
			break;

		case CLOWNZ80_OPCODE_OUT_REGISTER:
			UNIMPLEMENTED_Z80_INSTRUCTION("OUT (register)");
			break;

		case CLOWNZ80_OPCODE_OUT_NO_REGISTER:
			UNIMPLEMENTED_Z80_INSTRUCTION("OUT (no register)");
			break;

		case CLOWNZ80_OPCODE_SBC_HL:
			READ_SOURCE;
			READ_DESTINATION;

			source_value = ~(cc_u32f)source_value;

			result_value_with_carry_16bit = (cc_u32f)source_value + (cc_u32f)destination_value + ((state->f & FLAG_MASK_CARRY) != 0 ? 0 : 1);;
			result_value = result_value_with_carry_16bit & 0xFFFF;

			state->f = 0;

			CONDITION_SIGN_16BIT;
			CONDITION_ZERO;
			CONDITION_HALF_CARRY_16BIT;
			CONDITION_OVERFLOW_16BIT;
			CONDITION_CARRY_16BIT;

			state->f ^= FLAG_MASK_HALF_CARRY;
			state->f |= FLAG_MASK_ADD_SUBTRACT;

			WRITE_DESTINATION;

			/* This instruction requires an extra 7 cycles. */
			state->cycles += 7;

			break;

		case CLOWNZ80_OPCODE_ADC_HL:
			READ_SOURCE;
			READ_DESTINATION;

			result_value_with_carry_16bit = (cc_u32f)source_value + (cc_u32f)destination_value + ((state->f & FLAG_MASK_CARRY) != 0 ? 1 : 0);
			result_value = result_value_with_carry_16bit & 0xFFFF;

			state->f = 0;

			CONDITION_SIGN_16BIT;
			CONDITION_ZERO;
			CONDITION_HALF_CARRY_16BIT;
			CONDITION_OVERFLOW_16BIT;
			CONDITION_CARRY_16BIT;

			WRITE_DESTINATION;

			/* This instruction requires an extra 7 cycles. */
			state->cycles += 7;

			break;

		case CLOWNZ80_OPCODE_NEG:
			source_value = state->a;
			source_value = ~source_value;
			destination_value = 0;

			result_value_with_carry = destination_value + source_value + 1;
			result_value = result_value_with_carry & 0xFF;

			state->f = 0;
			CONDITION_CARRY;
			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_HALF_CARRY;
			CONDITION_OVERFLOW;

			state->f ^= FLAG_MASK_HALF_CARRY;
			state->f |= FLAG_MASK_ADD_SUBTRACT;

			state->a = result_value;

			break;

		case CLOWNZ80_OPCODE_IM:
			UNIMPLEMENTED_Z80_INSTRUCTION("IM");
			break;

		case CLOWNZ80_OPCODE_LD_I_A:
			/* This instruction requires an extra cycle. */
			state->cycles += 1;

			state->i = state->a;

			break;

		case CLOWNZ80_OPCODE_LD_R_A:
			/* This instruction requires an extra cycle. */
			state->cycles += 1;

			state->r = state->a;

			break;

		case CLOWNZ80_OPCODE_LD_A_I:
			/* This instruction requires an extra cycle. */
			state->cycles += 1;

			state->a = state->i;

			state->f &= FLAG_MASK_CARRY;
			state->f |= (state->a >> (7 - FLAG_BIT_SIGN)) & FLAG_MASK_SIGN;
			state->f |= state->a == 0 ? FLAG_MASK_ZERO : 0;
			/* TODO: IFF2 parity bit stuff. */

			break;

		case CLOWNZ80_OPCODE_LD_A_R:
			/* This instruction requires an extra cycle. */
			state->cycles += 1;

			state->a = state->r;

			state->f &= FLAG_MASK_CARRY;
			state->f |= (state->a >> (7 - FLAG_BIT_SIGN)) & FLAG_MASK_SIGN;
			state->f |= state->a == 0 ? FLAG_MASK_ZERO : 0;
			/* TODO: IFF2 parity bit stuff. */

			break;

		case CLOWNZ80_OPCODE_RRD:
		{
			const cc_u16f hl = ((cc_u16f)state->h << 8) | state->l;
			const cc_u8f hl_value = MemoryRead(state, callbacks, hl);
			const cc_u8f hl_high = (hl_value >> 4) & 0xF;
			const cc_u8f hl_low = (hl_value >> 0) & 0xF;
			const cc_u8f a_high = (state->a >> 4) & 0xF;
			const cc_u8f a_low = (state->a >> 0) & 0xF;

			/* This instruction requires an extra 4 cycles. */
			state->cycles += 4;

			MemoryWrite(state, callbacks, hl, (a_low << 4) | (hl_high << 0));
			result_value = (a_high << 4) | (hl_low << 0);

			state->f &= FLAG_MASK_CARRY;
			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_PARITY;

			state->a = result_value;

			break;
		}

		case CLOWNZ80_OPCODE_RLD:
		{
			const cc_u16f hl = ((cc_u16f)state->h << 8) | state->l;
			const cc_u8f hl_value = MemoryRead(state, callbacks, hl);
			const cc_u8f hl_high = (hl_value >> 4) & 0xF;
			const cc_u8f hl_low = (hl_value >> 0) & 0xF;
			const cc_u8f a_high = (state->a >> 4) & 0xF;
			const cc_u8f a_low = (state->a >> 0) & 0xF;

			/* This instruction requires an extra 4 cycles. */
			state->cycles += 4;

			MemoryWrite(state, callbacks, hl, (hl_low << 4) | (a_low << 0));
			result_value = (a_high << 4) | (hl_high << 0);

			state->f &= FLAG_MASK_CARRY;
			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_PARITY;

			state->a = result_value;

			break;
		}

		case CLOWNZ80_OPCODE_LDI:
		{
			const cc_u16f de = ((cc_u16f)state->d << 8) | state->e;
			const cc_u16f hl = ((cc_u16f)state->h << 8) | state->l;

			MemoryWrite(state, callbacks, de, MemoryRead(state, callbacks, hl));

			/* Increment 'hl'. */
			++state->l;
			state->l &= 0xFF;

			if (state->l == 0)
			{
				++state->h;
				state->h &= 0xFF;
			}

			/* Increment 'de'. */
			++state->e;
			state->e &= 0xFF;

			if (state->e == 0)
			{
				++state->d;
				state->d &= 0xFF;
			}

			/* Decrement 'bc'. */
			--state->c;
			state->c &= 0xFF;

			if (state->c == 0xFF)
			{
				--state->b;
				state->b &= 0xFF;
			}

			state->f &= FLAG_MASK_CARRY | FLAG_MASK_ZERO | FLAG_MASK_SIGN;
			state->f |= (state->b | state->c) != 0 ? FLAG_MASK_PARITY_OVERFLOW : 0;

			/* This instruction requires an extra 2 cycles. */
			state->cycles += 2;

			break;
		}

		case CLOWNZ80_OPCODE_LDD:
		{
			const cc_u16f de = ((cc_u16f)state->d << 8) | state->e;
			const cc_u16f hl = ((cc_u16f)state->h << 8) | state->l;

			MemoryWrite(state, callbacks, de, MemoryRead(state, callbacks, hl));

			/* Decrement 'hl'. */
			--state->l;
			state->l &= 0xFF;

			if (state->l == 0xFF)
			{
				--state->h;
				state->h &= 0xFF;
			}

			/* Decrement 'de'. */
			--state->e;
			state->e &= 0xFF;

			if (state->e == 0xFF)
			{
				--state->d;
				state->d &= 0xFF;
			}

			/* Decrement 'bc'. */
			--state->c;
			state->c &= 0xFF;

			if (state->c == 0xFF)
			{
				--state->b;
				state->b &= 0xFF;
			}

			state->f &= FLAG_MASK_CARRY | FLAG_MASK_ZERO | FLAG_MASK_SIGN;
			state->f |= (state->b | state->c) != 0 ? FLAG_MASK_PARITY_OVERFLOW : 0;

			/* This instruction requires an extra 2 cycles. */
			state->cycles += 2;

			break;
		}

		case CLOWNZ80_OPCODE_LDIR:
		{
			const cc_u16f de = ((cc_u16f)state->d << 8) | state->e;
			const cc_u16f hl = ((cc_u16f)state->h << 8) | state->l;

			MemoryWrite(state, callbacks, de, MemoryRead(state, callbacks, hl));

			/* Increment 'hl'. */
			++state->l;
			state->l &= 0xFF;

			if (state->l == 0)
			{
				++state->h;
				state->h &= 0xFF;
			}

			/* Increment 'de'. */
			++state->e;
			state->e &= 0xFF;

			if (state->e == 0)
			{
				++state->d;
				state->d &= 0xFF;
			}

			/* Decrement 'bc'. */
			--state->c;
			state->c &= 0xFF;

			if (state->c == 0xFF)
			{
				--state->b;
				state->b &= 0xFF;
			}

			state->f &= FLAG_MASK_CARRY | FLAG_MASK_ZERO | FLAG_MASK_SIGN;
			state->f |= (state->b | state->c) != 0 ? FLAG_MASK_PARITY_OVERFLOW : 0;

			/* This instruction requires an extra 2 cycles. */
			state->cycles += 2;

			if ((state->f & FLAG_MASK_PARITY_OVERFLOW) != 0)
			{
				/* An extra 5 cycles are needed here. */
				state->cycles += 5;

				state->program_counter -= 2;
			}

			break;
		}

		case CLOWNZ80_OPCODE_LDDR:
		{
			const cc_u16f de = ((cc_u16f)state->d << 8) | state->e;
			const cc_u16f hl = ((cc_u16f)state->h << 8) | state->l;

			MemoryWrite(state, callbacks, de, MemoryRead(state, callbacks, hl));

			/* Decrement 'hl'. */
			--state->l;
			state->l &= 0xFF;

			if (state->l == 0xFF)
			{
				--state->h;
				state->h &= 0xFF;
			}

			/* Decrement 'de'. */
			--state->e;
			state->e &= 0xFF;

			if (state->e == 0xFF)
			{
				--state->d;
				state->d &= 0xFF;
			}

			/* Decrement 'bc'. */
			--state->c;
			state->c &= 0xFF;

			if (state->c == 0xFF)
			{
				--state->b;
				state->b &= 0xFF;
			}

			state->f &= FLAG_MASK_CARRY | FLAG_MASK_ZERO | FLAG_MASK_SIGN;
			state->f |= (state->b | state->c) != 0 ? FLAG_MASK_PARITY_OVERFLOW : 0;

			/* This instruction requires an extra 2 cycles. */
			state->cycles += 2;

			if ((state->f & FLAG_MASK_PARITY_OVERFLOW) != 0)
			{
				/* An extra 5 cycles are needed here. */
				state->cycles += 5;

				state->program_counter -= 2;
			}

			break;
		}

		case CLOWNZ80_OPCODE_CPI:
		{
			const cc_u16f hl = ((cc_u16f)state->h << 8) | state->l;

			source_value = MemoryRead(state, callbacks, hl);
			destination_value = state->a;
			result_value = destination_value - source_value;

			/* Increment 'hl'. */
			++state->l;
			state->l &= 0xFF;

			if (state->l == 0)
			{
				++state->h;
				state->h &= 0xFF;
			}

			/* Decrement 'bc'. */
			--state->c;
			state->c &= 0xFF;

			if (state->c == 0xFF)
			{
				--state->b;
				state->b &= 0xFF;
			}

			state->f &= FLAG_MASK_CARRY;
			state->f |= (state->b | state->c) != 0 ? FLAG_MASK_PARITY_OVERFLOW : 0;
			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_HALF_CARRY;

			state->f |= FLAG_MASK_ADD_SUBTRACT;

			/* This instruction requires an extra 2 cycles. */
			state->cycles += 2;

			break;
		}

		case CLOWNZ80_OPCODE_CPD:
		{
			const cc_u16f hl = ((cc_u16f)state->h << 8) | state->l;

			source_value = MemoryRead(state, callbacks, hl);
			destination_value = state->a;
			result_value = destination_value - source_value;

			/* Decrement 'hl'. */
			--state->l;
			state->l &= 0xFF;

			if (state->l == 0xFF)
			{
				--state->h;
				state->h &= 0xFF;
			}

			/* Decrement 'bc'. */
			--state->c;
			state->c &= 0xFF;

			if (state->c == 0xFF)
			{
				--state->b;
				state->b &= 0xFF;
			}

			state->f &= FLAG_MASK_CARRY;
			state->f |= (state->b | state->c) != 0 ? FLAG_MASK_PARITY_OVERFLOW : 0;
			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_HALF_CARRY;

			state->f |= FLAG_MASK_ADD_SUBTRACT;

			/* This instruction requires an extra 2 cycles. */
			state->cycles += 2;

			break;
		}

		case CLOWNZ80_OPCODE_CPIR:
		{
			const cc_u16f hl = ((cc_u16f)state->h << 8) | state->l;

			source_value = MemoryRead(state, callbacks, hl);
			destination_value = state->a;
			result_value = destination_value - source_value;

			/* Increment 'hl'. */
			++state->l;
			state->l &= 0xFF;

			if (state->l == 0)
			{
				++state->h;
				state->h &= 0xFF;
			}

			/* Decrement 'bc'. */
			--state->c;
			state->c &= 0xFF;

			if (state->c == 0xFF)
			{
				--state->b;
				state->b &= 0xFF;
			}

			state->f &= FLAG_MASK_CARRY;
			state->f |= (state->b | state->c) != 0 ? FLAG_MASK_PARITY_OVERFLOW : 0;
			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_HALF_CARRY;

			state->f |= FLAG_MASK_ADD_SUBTRACT;

			/* This instruction requires an extra 2 cycles. */
			state->cycles += 2;

			if ((state->f & FLAG_MASK_PARITY_OVERFLOW) != 0 && (state->f & FLAG_MASK_ZERO) == 0)
			{
				/* An extra 5 cycles are needed here. */
				state->cycles += 5;

				state->program_counter -= 2;
			}

			break;
		}

		case CLOWNZ80_OPCODE_CPDR:
		{
			const cc_u16f hl = ((cc_u16f)state->h << 8) | state->l;

			source_value = MemoryRead(state, callbacks, hl);
			destination_value = state->a;
			result_value = destination_value - source_value;

			/* Decrement 'hl'. */
			--state->l;
			state->l &= 0xFF;

			if (state->l == 0xFF)
			{
				--state->h;
				state->h &= 0xFF;
			}

			/* Decrement 'bc'. */
			--state->c;
			state->c &= 0xFF;

			if (state->c == 0xFF)
			{
				--state->b;
				state->b &= 0xFF;
			}

			state->f &= FLAG_MASK_CARRY;
			state->f |= (state->b | state->c) != 0 ? FLAG_MASK_PARITY_OVERFLOW : 0;
			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_HALF_CARRY;

			state->f |= FLAG_MASK_ADD_SUBTRACT;

			/* This instruction requires an extra 2 cycles. */
			state->cycles += 2;

			if ((state->f & FLAG_MASK_PARITY_OVERFLOW) != 0 && (state->f & FLAG_MASK_ZERO) == 0)
			{
				/* An extra 5 cycles are needed here. */
				state->cycles += 5;

				state->program_counter -= 2;
			}

			break;
		}

		case CLOWNZ80_OPCODE_INI:
			UNIMPLEMENTED_Z80_INSTRUCTION("INI");
			break;

		case CLOWNZ80_OPCODE_IND:
			UNIMPLEMENTED_Z80_INSTRUCTION("IND");
			break;

		case CLOWNZ80_OPCODE_INIR:
			UNIMPLEMENTED_Z80_INSTRUCTION("INIR");
			break;

		case CLOWNZ80_OPCODE_INDR:
			UNIMPLEMENTED_Z80_INSTRUCTION("INDR");
			break;

		case CLOWNZ80_OPCODE_OUTI:
			UNIMPLEMENTED_Z80_INSTRUCTION("OTDI");
			break;

		case CLOWNZ80_OPCODE_OUTD:
			UNIMPLEMENTED_Z80_INSTRUCTION("OUTD");
			break;

		case CLOWNZ80_OPCODE_OTIR:
			UNIMPLEMENTED_Z80_INSTRUCTION("OTIR");
			break;

		case CLOWNZ80_OPCODE_OTDR:
			UNIMPLEMENTED_Z80_INSTRUCTION("OTDR");
			break;

		#undef UNIMPLEMENTED_Z80_INSTRUCTION
	}
}

void ClownZ80_Constant_Initialise(void)
{
#ifdef CLOWNZ80_PRECOMPUTE_INSTRUCTION_METADATA
	cc_u16f i;

	/* Pre-compute instruction metadata, to speed up opcode decoding. */
	for (i = 0; i < 0x100; ++i)
	{
		ClownZ80_DecodeInstructionMetadata(&instruction_metadata_lookup_normal[CLOWNZ80_REGISTER_MODE_HL][i], CLOWNZ80_INSTRUCTION_MODE_NORMAL, CLOWNZ80_REGISTER_MODE_HL, i);
		ClownZ80_DecodeInstructionMetadata(&instruction_metadata_lookup_normal[CLOWNZ80_REGISTER_MODE_IX][i], CLOWNZ80_INSTRUCTION_MODE_NORMAL, CLOWNZ80_REGISTER_MODE_IX, i);
		ClownZ80_DecodeInstructionMetadata(&instruction_metadata_lookup_normal[CLOWNZ80_REGISTER_MODE_IY][i], CLOWNZ80_INSTRUCTION_MODE_NORMAL, CLOWNZ80_REGISTER_MODE_IY, i);

		ClownZ80_DecodeInstructionMetadata(&instruction_metadata_lookup_bits[CLOWNZ80_REGISTER_MODE_HL][i], CLOWNZ80_INSTRUCTION_MODE_BITS, CLOWNZ80_REGISTER_MODE_HL, i);
		ClownZ80_DecodeInstructionMetadata(&instruction_metadata_lookup_bits[CLOWNZ80_REGISTER_MODE_IX][i], CLOWNZ80_INSTRUCTION_MODE_BITS, CLOWNZ80_REGISTER_MODE_IX, i);
		ClownZ80_DecodeInstructionMetadata(&instruction_metadata_lookup_bits[CLOWNZ80_REGISTER_MODE_IY][i], CLOWNZ80_INSTRUCTION_MODE_BITS, CLOWNZ80_REGISTER_MODE_IY, i);

		ClownZ80_DecodeInstructionMetadata(&instruction_metadata_lookup_misc[i], CLOWNZ80_INSTRUCTION_MODE_MISC, CLOWNZ80_REGISTER_MODE_HL, i);
	}
#endif
}

void ClownZ80_State_Initialise(ClownZ80_State* const state)
{
	ClownZ80_Reset(state);

	/* Update on the next cycle. */
	state->cycles = 1;
}

void ClownZ80_Reset(ClownZ80_State* const state)
{
	/* Revert back to a prefix-free mode. */
	state->register_mode = CLOWNZ80_REGISTER_MODE_HL;

	state->program_counter = 0;

	/* The official Z80 manual claims that this happens ('z80cpu_um.pdf' page 18). */
	state->interrupts_enabled = cc_false;

	state->interrupt_pending = cc_false;
}

void ClownZ80_Interrupt(ClownZ80_State* const state, const cc_bool assert_interrupt)
{
	state->interrupt_pending = assert_interrupt;
}

cc_u16f ClownZ80_DoInstruction(ClownZ80_State* const state, const ClownZ80_ReadAndWriteCallbacks* const callbacks)
{
	/* Process new instruction. */
	Z80Instruction instruction;

#ifndef CLOWNZ80_PRECOMPUTE_INSTRUCTION_METADATA
	ClownZ80_InstructionMetadata metadata;
	instruction.metadata = &metadata;
#endif

	state->cycles = 0;

	DecodeInstruction(state, callbacks, &instruction);

	ExecuteInstruction(state, callbacks, &instruction);

	/* Perform interrupt after processing the instruction. */
	/* TODO: The other interrupt modes. */
	if (state->interrupt_pending
		&& state->interrupts_enabled
		/* Interrupts should not be able to occur directly after a prefix instruction. */
		&& instruction.metadata->opcode != CLOWNZ80_OPCODE_DD_PREFIX
		&& instruction.metadata->opcode != CLOWNZ80_OPCODE_FD_PREFIX
		/* Curiously, interrupts do not occur directly after 'EI' instructions either. */
		&& instruction.metadata->opcode != CLOWNZ80_OPCODE_EI)
	{
		state->interrupts_enabled = cc_false;
		state->interrupt_pending = cc_false;

		/* TODO: Other interrupt durations. */
		state->cycles += 13; /* Interrupt mode 1 duration. */

		--state->stack_pointer;
		state->stack_pointer &= 0xFFFF;
		callbacks->write((void*)callbacks->user_data, state->stack_pointer, state->program_counter >> 8);

		--state->stack_pointer;
		state->stack_pointer &= 0xFFFF;
		callbacks->write((void*)callbacks->user_data, state->stack_pointer, state->program_counter & 0xFF);

		state->program_counter = 0x38;
	}

	return state->cycles;
}
