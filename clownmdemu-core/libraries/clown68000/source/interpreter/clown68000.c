/*
    clown68000 - A Motorola 68000 emulator.
    Copyright (C) 2021-2025 Clownacy

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

/*
TODO:

68k memory access order:
https://gendev.spritesmind.net/forum/viewtopic.php?p=36896#p36896

Z80 reset stuff:
https://gendev.spritesmind.net/forum/viewtopic.php?p=36118#p36118

ABCD/SBCD quirks:
http://gendev.spritesmind.net/forum/viewtopic.php?f=2&t=1964

Address error behaviour:
https://github.com/TomHarte/ProcessorTests/issues/28

*/

#include "clown68000.h"

#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

/* This is needed so that '_POSIX_VERSION' has the chance to defined. */
#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
	#include <unistd.h>
#endif

#include "../common/instruction.h"
#include "../common/opcode.h"

/* 'setjmp' is dreadfully slow on macOS (and presumably the BSDs),
   so use 'sigsetjmp' instead if it is available. */
#if defined(_POSIX_VERSION) && _POSIX_VERSION >= 200112L
	#define clown68000_jmp_buf sigjmp_buf
	#define clown68000_setjmp(env) sigsetjmp(env, 0)
	#define clown68000_longjmp(env, val) siglongjmp(env, val)
#else
	#define clown68000_jmp_buf jmp_buf
	#define clown68000_setjmp(env) setjmp(env)
	#define clown68000_longjmp(env, val) longjmp(env, val)
#endif

enum
{
	CONDITION_CODE_CARRY_BIT    = 0,
	CONDITION_CODE_OVERFLOW_BIT = 1,
	CONDITION_CODE_ZERO_BIT     = 2,
	CONDITION_CODE_NEGATIVE_BIT = 3,
	CONDITION_CODE_EXTEND_BIT   = 4,

	CONDITION_CODE_CARRY    = 1 << CONDITION_CODE_CARRY_BIT,
	CONDITION_CODE_OVERFLOW = 1 << CONDITION_CODE_OVERFLOW_BIT,
	CONDITION_CODE_ZERO     = 1 << CONDITION_CODE_ZERO_BIT,
	CONDITION_CODE_NEGATIVE = 1 << CONDITION_CODE_NEGATIVE_BIT,
	CONDITION_CODE_EXTEND   = 1 << CONDITION_CODE_EXTEND_BIT,
	CONDITION_CODE_REGISTER_MASK = CONDITION_CODE_EXTEND | CONDITION_CODE_NEGATIVE | CONDITION_CODE_ZERO | CONDITION_CODE_OVERFLOW | CONDITION_CODE_CARRY,

	STATUS_INTERRUPT_MASK   = 7 << 8,
	STATUS_SUPERVISOR       = 1 << 13,
	STATUS_TRACE            = 1 << 15,
	STATUS_REGISTER_MASK    = STATUS_TRACE | STATUS_SUPERVISOR | STATUS_INTERRUPT_MASK | CONDITION_CODE_REGISTER_MASK
};

typedef	enum DecodedAddressModeType
{
	DECODED_ADDRESS_MODE_TYPE_REGISTER,
	DECODED_ADDRESS_MODE_TYPE_MEMORY,
	DECODED_ADDRESS_MODE_TYPE_STATUS_REGISTER,
	DECODED_ADDRESS_MODE_TYPE_CONDITION_CODE_REGISTER
} DecodedAddressModeType;

struct Stuff;

typedef struct DecodedMemoryAddressMode
{
	cc_u32f address;
	cc_u32f(*read)(struct Stuff *stuff, cc_u32f address);
	void (*write)(struct Stuff *stuff, cc_u32f address, cc_u32f value);
} DecodedMemoryAddressMode;

typedef struct DecodedAddressMode
{
	DecodedAddressModeType type;
	union
	{
		struct
		{
			cc_u32l *address;
			cc_u32f operation_size_bitmask;
		} reg;
		DecodedMemoryAddressMode memory;
	} data;
} DecodedAddressMode;

typedef struct Stuff
{
	Clown68000_State *state;
	const Clown68000_ReadWriteCallbacks *callbacks;
	cc_u8f cycles_left_in_instruction;
	cc_u32f cycles_done;
	struct
	{
		clown68000_jmp_buf context;
		cc_u16f vector_offset;
	} exception;
	SplitOpcode opcode;
	cc_u32f operation_size, msb_bit_index;
	DecodedAddressMode source_decoded_address_mode, destination_decoded_address_mode;
	cc_u32f source_value, destination_value, result_value;
	cc_u32f starting_program_counter;
} Stuff;

/* Error callback. */
/* TODO: Remove this once all instructions are implemented. */

static void (*clown68000_error_callback)(void *user_data, const char *format, va_list arg);
static void *clown68000_error_callback_user_data;

void Clown68000_SetErrorCallback(void (*error_callback)(void *user_data, const char *format, va_list arg), const void* const user_data)
{
	clown68000_error_callback = error_callback;
	clown68000_error_callback_user_data = (void*)user_data;
}

CC_ATTRIBUTE_PRINTF(1, 2) static void Clown68000_PrintError(const char *format, ...)
{
	if (clown68000_error_callback != NULL)
	{
		va_list args;
		va_start(args, format);
		clown68000_error_callback(clown68000_error_callback_user_data, format, args);
		va_end(args);
	}
}

/* Exception forward-declarations. */

static void Group0Exception(Stuff *stuff, cc_u16f vector_offset, cc_u32f access_address, cc_bool is_a_read);

/* Memory reads */

static cc_u32f ReadAddress(Stuff *stuff, cc_u32f address)
{
	(void)stuff;

	return address;
}

static cc_u32f ReadByte(Stuff *stuff, cc_u32f address)
{
	const Clown68000_ReadWriteCallbacks* const callbacks = stuff->callbacks;
	const cc_bool odd = (address & 1) != 0;

	return (callbacks->read_callback(callbacks->user_data, (address / 2) & 0x7FFFFF, (cc_bool)!odd, odd, stuff->cycles_done) >> (odd ? 0 : 8)) & 0xFF;
}

static cc_u32f ReadWord(Stuff *stuff, cc_u32f address)
{
	const Clown68000_ReadWriteCallbacks* const callbacks = stuff->callbacks;

	/* TODO: I've heard that the exception should occur after the read is completed. */
	if ((address & 1) != 0)
		Group0Exception(stuff, 3, address, cc_true);

	return callbacks->read_callback(callbacks->user_data, (address / 2) & 0x7FFFFF, cc_true, cc_true, stuff->cycles_done);
}

static cc_u32f ReadLongWord(Stuff *stuff, cc_u32f address)
{
	cc_u32f value;

	value = 0;
	value |= (cc_u32f)ReadWord(stuff, address + 0) << 16;
	value |= (cc_u32f)ReadWord(stuff, address + 2) << 0;

	return value;
}

/* Used for pre-decrement and stack. */
static cc_u32f ReadLongWordBackwards(Stuff *stuff, cc_u32f address)
{
	cc_u32f value;

	value = 0;
	value |= (cc_u32f)ReadWord(stuff, address + 2) << 0;
	value |= (cc_u32f)ReadWord(stuff, address + 0) << 16;

	return value;
}

/* Memory writes */

static void WriteByte(Stuff *stuff, cc_u32f address, cc_u32f value)
{
	const Clown68000_ReadWriteCallbacks* const callbacks = stuff->callbacks;
	const cc_bool odd = (address & 1) != 0;
	const cc_u16f byte = value & 0xFF;

	callbacks->write_callback(callbacks->user_data, (address / 2) & 0x7FFFFF, (cc_bool)!odd, odd, stuff->cycles_done, byte | byte << 8);
}

static void WriteWord(Stuff *stuff, cc_u32f address, cc_u32f value)
{
	const Clown68000_ReadWriteCallbacks* const callbacks = stuff->callbacks;

	/* TODO: I've heard that the exception should occur after the write is completed. */
	if ((address & 1) != 0)
		Group0Exception(stuff, 3, address, cc_false);

	callbacks->write_callback(callbacks->user_data, (address / 2) & 0x7FFFFF, cc_true, cc_true, stuff->cycles_done, value & 0xFFFF);
}

static void WriteLongWord(Stuff *stuff, cc_u32f address, cc_u32f value)
{
	WriteWord(stuff, address + 0, value >> 16);
	WriteWord(stuff, address + 2, value >> 0);
}

/* Used for pre-decrement and stack. */
static void WriteLongWordBackwards(Stuff *stuff, cc_u32f address, cc_u32f value)
{
	WriteWord(stuff, address + 2, value >> 0);
	WriteWord(stuff, address + 0, value >> 16);
}

/* Supervisor mode */

static void SetSupervisorMode(Clown68000_State *state, cc_bool supervisor_mode)
{
	const cc_bool already_supervisor_mode = (state->status_register & STATUS_SUPERVISOR) != 0;

	if (supervisor_mode)
	{
		if (!already_supervisor_mode)
		{
			state->status_register |= STATUS_SUPERVISOR;
			state->user_stack_pointer = state->address_registers[7];
			state->address_registers[7] = state->supervisor_stack_pointer;
		}
	}
	else
	{
		if (already_supervisor_mode)
		{
			state->status_register &= ~STATUS_SUPERVISOR;
			state->supervisor_stack_pointer = state->address_registers[7];
			state->address_registers[7] = state->user_stack_pointer;
		}
	}
}

/* Exceptions */

static void IncrementRegister(cc_u32l* const register_pointer, const cc_u32f delta)
{
	*register_pointer += delta;
	*register_pointer &= 0xFFFFFFFF;
}

static void DecrementRegister(cc_u32l* const register_pointer, const cc_u32f delta)
{
	*register_pointer -= delta;
	*register_pointer &= 0xFFFFFFFF;
}

static void IncrementAddressRegister(Clown68000_State* const state, const cc_u8f address_register_index, const cc_u32f delta)
{
	IncrementRegister(&state->address_registers[address_register_index], delta);
}

static void DecrementAddressRegister(Clown68000_State* const state, const cc_u8f address_register_index, const cc_u32f delta)
{
	DecrementRegister(&state->address_registers[address_register_index], delta);
}

static void IncrementProgramCounter(Clown68000_State* const state, const cc_u32f delta)
{
	IncrementRegister(&state->program_counter, delta);
}

static void DoInterrupt(Stuff *stuff, cc_u16f vector_offset)
{
	Clown68000_State* const state = stuff->state;
	const cc_u16l copy_status_register = state->status_register; /* Preserve the original supervisor bit. */

	/* Exit trace mode. */
	state->status_register &= ~STATUS_TRACE;
	/* Set supervisor bit */
	SetSupervisorMode(state, cc_true);

	DecrementAddressRegister(state, 7, 4);
	WriteLongWordBackwards(stuff, state->address_registers[7], state->program_counter);
	DecrementAddressRegister(state, 7, 2);
	WriteWord(stuff, state->address_registers[7], copy_status_register);

	state->program_counter = ReadLongWord(stuff, vector_offset * 4);

	stuff->cycles_left_in_instruction += 30;
}

static void Group1Or2Exception(Stuff *stuff, cc_u16f vector_offset)
{
	stuff->exception.vector_offset = vector_offset;
	clown68000_longjmp(stuff->exception.context, 2);
}

static void Group0Exception(Stuff *stuff, cc_u16f vector_offset, cc_u32f access_address, cc_bool is_a_read)
{
	Clown68000_State* const state = stuff->state;

	/* If a data or address error occurs during group 0 exception processing, then the CPU halts. */
	if ((state->address_registers[7] & 1) != 0)
	{
		state->halted = cc_true;
	}
	else
	{
		DoInterrupt(stuff, vector_offset);

		/* Push extra information to the stack. */
		DecrementAddressRegister(state, 7, 2);
		WriteWord(stuff, state->address_registers[7], state->instruction_register);
		DecrementAddressRegister(state, 7, 4);
		WriteLongWordBackwards(stuff, state->address_registers[7], access_address);
		DecrementAddressRegister(state, 7, 2);
		/* TODO - Function code and 'Instruction/Not' bit. */
		/* According to the test suite, there really is a partial phantom copy of the instruction register here. */
		WriteWord(stuff, state->address_registers[7], (state->instruction_register & 0xFFE0) | (is_a_read << 4) | 0xE);
	}

	clown68000_longjmp(stuff->exception.context, 1);
}

/* Misc. utility */

static void DecodeMemoryAddressMode(Stuff* const stuff, DecodedMemoryAddressMode* const decoded_memory_address_mode, const unsigned int operation_size_in_bytes, const AddressMode address_mode, const unsigned int address_mode_register)
{
	Clown68000_State* const state = stuff->state;
	const cc_bool longword = operation_size_in_bytes == 4;

	cc_u32f address;

	switch (operation_size_in_bytes)
	{
		case 0:
			decoded_memory_address_mode->read = ReadAddress;
			decoded_memory_address_mode->write = NULL;
			break;

		default:
			assert(cc_false);
			/* Fallthrough */
		case 1:
			decoded_memory_address_mode->read = ReadByte;
			decoded_memory_address_mode->write = WriteByte;
			break;

		case 2:
			decoded_memory_address_mode->read = ReadWord;
			decoded_memory_address_mode->write = WriteWord;
			break;

		case 4:
			if (address_mode == ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_PREDECREMENT)
			{
				decoded_memory_address_mode->read = ReadLongWordBackwards;
				decoded_memory_address_mode->write = WriteLongWordBackwards;
			}
			else
			{
				decoded_memory_address_mode->read = ReadLongWord;
				decoded_memory_address_mode->write = WriteLongWord;
			}

			break;
	}

	if (address_mode == ADDRESS_MODE_SPECIAL && address_mode_register == ADDRESS_MODE_REGISTER_SPECIAL_ABSOLUTE_SHORT)
	{
		/* Absolute short */
		const cc_u32f short_address = ReadWord(stuff, state->program_counter);

		address = CC_SIGN_EXTEND_ULONG(15, short_address);
		IncrementProgramCounter(state, 2);

		stuff->cycles_left_in_instruction += longword ? 12 : 8;
	}
	else if (address_mode == ADDRESS_MODE_SPECIAL && address_mode_register == ADDRESS_MODE_REGISTER_SPECIAL_ABSOLUTE_LONG)
	{
		/* Absolute long */
		address = ReadLongWord(stuff, state->program_counter);
		IncrementProgramCounter(state, 4);

		stuff->cycles_left_in_instruction += longword ? 16 : 12;
	}
	else if (address_mode == ADDRESS_MODE_SPECIAL && address_mode_register == ADDRESS_MODE_REGISTER_SPECIAL_IMMEDIATE)
	{
		/* Immediate value */
		if (operation_size_in_bytes == 1)
		{
			/* A byte-sized immediate value occupies two bytes of space */
			address = state->program_counter + 1;
			IncrementProgramCounter(state, 2);
		}
		else
		{
			address = state->program_counter;
			IncrementProgramCounter(state, operation_size_in_bytes);
		}

		stuff->cycles_left_in_instruction += longword ? 8 : 4;
	}
	else if (address_mode == ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT)
	{
		/* Address register indirect */
		address = state->address_registers[address_mode_register];

		stuff->cycles_left_in_instruction += longword ? 8 : 4;
	}
	else if (address_mode == ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_PREDECREMENT)
	{
		/* Address register indirect with predecrement */

		/* The stack pointer moves two bytes instead of one byte, for alignment purposes */
		const cc_u8f increment_decrement_size = (address_mode_register == 7 && operation_size_in_bytes == 1) ? 2 : operation_size_in_bytes;

		DecrementAddressRegister(state, address_mode_register, increment_decrement_size);
		address = state->address_registers[address_mode_register];

		stuff->cycles_left_in_instruction += longword ? 10 : 6;
	}
	else if (address_mode == ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_POSTINCREMENT)
	{
		/* Address register indirect with postincrement */

		/* The stack pointer moves two bytes instead of one byte, for alignment purposes */
		const cc_u8f increment_decrement_size = (address_mode_register == 7 && operation_size_in_bytes == 1) ? 2 : operation_size_in_bytes;

		address = state->address_registers[address_mode_register];
		IncrementAddressRegister(state, address_mode_register, increment_decrement_size);

		stuff->cycles_left_in_instruction += longword ? 8 : 4;
	}
	else
	{
		if (address_mode == ADDRESS_MODE_SPECIAL && (address_mode_register == ADDRESS_MODE_REGISTER_SPECIAL_PROGRAM_COUNTER_WITH_DISPLACEMENT || address_mode_register == ADDRESS_MODE_REGISTER_SPECIAL_PROGRAM_COUNTER_WITH_INDEX))
		{
			/* Program counter used as base address */
			address = state->program_counter;
		}
		else
		{
			/* Address register used as base address */
			address = state->address_registers[address_mode_register];
		}

		if (address_mode == ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_DISPLACEMENT || (address_mode == ADDRESS_MODE_SPECIAL && address_mode_register == ADDRESS_MODE_REGISTER_SPECIAL_PROGRAM_COUNTER_WITH_DISPLACEMENT))
		{
			/* Add displacement */
			const cc_u32f displacement = ReadWord(stuff, state->program_counter);

			address += CC_SIGN_EXTEND_ULONG(15, displacement);
			IncrementProgramCounter(state, 2);

			stuff->cycles_left_in_instruction += longword ? 12 : 8;
		}
		else if (address_mode == ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_INDEX || (address_mode == ADDRESS_MODE_SPECIAL && address_mode_register == ADDRESS_MODE_REGISTER_SPECIAL_PROGRAM_COUNTER_WITH_INDEX))
		{
			/* Add index register and index literal */
			const cc_u32f extension_word = ReadWord(stuff, state->program_counter);
			const cc_bool is_address_register = (extension_word & 0x8000) != 0;
			const cc_u32f displacement_reg = (extension_word >> 12) & 7;
			const cc_bool is_longword = (extension_word & 0x0800) != 0;
			const cc_u32f displacement_literal_value = CC_SIGN_EXTEND_ULONG(7, extension_word);
			const cc_u32f displacement_reg_value = CC_SIGN_EXTEND_ULONG(is_longword ? 31 : 15, (is_address_register ? state->address_registers : state->data_registers)[displacement_reg]);

			address += displacement_reg_value;
			address += displacement_literal_value;
			IncrementProgramCounter(state, 2);

			stuff->cycles_left_in_instruction += longword ? 14 : 10;
		}
	}

	decoded_memory_address_mode->address = address & 0xFFFFFFFF;
}

static void DecodeAddressMode(Stuff* const stuff, DecodedAddressMode* const decoded_address_mode, const unsigned int operation_size_in_bytes, const AddressMode address_mode, const unsigned int address_mode_register)
{
	Clown68000_State* const state = stuff->state;

	switch (address_mode)
	{
		case ADDRESS_MODE_DATA_REGISTER:
		case ADDRESS_MODE_ADDRESS_REGISTER:
			/* Register */
			decoded_address_mode->type = DECODED_ADDRESS_MODE_TYPE_REGISTER;
			decoded_address_mode->data.reg.address = &(address_mode == ADDRESS_MODE_ADDRESS_REGISTER ? state->address_registers : state->data_registers)[address_mode_register];
			decoded_address_mode->data.reg.operation_size_bitmask = (0xFFFFFFFF >> (32 - operation_size_in_bytes * 8));
			break;

		case ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT:
		case ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_POSTINCREMENT:
		case ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_PREDECREMENT:
		case ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_DISPLACEMENT:
		case ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_INDEX:
		case ADDRESS_MODE_SPECIAL:
			/* Memory access */
			decoded_address_mode->type = DECODED_ADDRESS_MODE_TYPE_MEMORY;
			DecodeMemoryAddressMode(stuff, &decoded_address_mode->data.memory, operation_size_in_bytes, address_mode, address_mode_register);
			break;
	}
}

static cc_u32f GetValueUsingDecodedAddressMode(Stuff *stuff, DecodedAddressMode *decoded_address_mode)
{
	cc_u32f value = 0;

	Clown68000_State* const state = stuff->state;

	switch (decoded_address_mode->type)
	{
		case DECODED_ADDRESS_MODE_TYPE_REGISTER:
			value = *decoded_address_mode->data.reg.address & decoded_address_mode->data.reg.operation_size_bitmask;
			break;

		case DECODED_ADDRESS_MODE_TYPE_MEMORY:
			value = decoded_address_mode->data.memory.read(stuff, decoded_address_mode->data.memory.address);
			break;

		case DECODED_ADDRESS_MODE_TYPE_STATUS_REGISTER:
			value = state->status_register;
			break;

		case DECODED_ADDRESS_MODE_TYPE_CONDITION_CODE_REGISTER:
			value = state->status_register & 0xFF;
			break;
	}

	return value;
}

static void SetValueUsingDecodedAddressMode(Stuff *stuff, DecodedAddressMode *decoded_address_mode, cc_u32f value)
{
	Clown68000_State* const state = stuff->state;

	switch (decoded_address_mode->type)
	{
		case DECODED_ADDRESS_MODE_TYPE_REGISTER:
		{
			const cc_u32f destination_value = *decoded_address_mode->data.reg.address;
			const cc_u32f operation_size_bitmask = decoded_address_mode->data.reg.operation_size_bitmask;
			*decoded_address_mode->data.reg.address = (value & operation_size_bitmask) | (destination_value & ~operation_size_bitmask);
			break;
		}

		case DECODED_ADDRESS_MODE_TYPE_MEMORY:
			decoded_address_mode->data.memory.write(stuff, decoded_address_mode->data.memory.address, value);
			break;

		case DECODED_ADDRESS_MODE_TYPE_STATUS_REGISTER:
			SetSupervisorMode(state, (value & STATUS_SUPERVISOR) != 0);
			state->status_register = value & STATUS_REGISTER_MASK;

			break;

		case DECODED_ADDRESS_MODE_TYPE_CONDITION_CODE_REGISTER:
			state->status_register = (state->status_register & ~CONDITION_CODE_REGISTER_MASK) | (value & CONDITION_CODE_REGISTER_MASK);
			break;
	}
}

static cc_bool IsOpcodeConditionTrue(Clown68000_State *state, cc_u16f opcode)
{
	const cc_bool carry = (state->status_register & CONDITION_CODE_CARRY) != 0;
	const cc_bool overflow = (state->status_register & CONDITION_CODE_OVERFLOW) != 0;
	const cc_bool zero = (state->status_register & CONDITION_CODE_ZERO) != 0;
	const cc_bool negative = (state->status_register & CONDITION_CODE_NEGATIVE) != 0;

	switch ((opcode >> 8) & 0xF)
	{
		case 0x0:
			/* True */
			return cc_true;

		case 0x1:
			/* False */
			return cc_false;

		case 0x2:
			/* Higher */
			return !carry && !zero;

		case 0x3:
			/* Lower or same */
			return carry || zero;

		case 0x4:
			/* Carry clear */
			return !carry;

		case 0x5:
			/* Carry set */
			return carry;

		case 0x6:
			/* Not equal */
			return !zero;

		case 0x7:
			/* Equal */
			return zero;

		case 0x8:
			/* Overflow clear */
			return !overflow;

		case 0x9:
			/* Overflow set */
			return overflow;

		case 0xA:
			/* Plus */
			return !negative;

		case 0xB:
			/* Minus */
			return negative;

		case 0xC:
			/* Greater or equal */
			return negative == overflow;

		case 0xD:
			/* Less than */
			return negative != overflow;

		case 0xE:
			/* Greater than */
			return !zero && negative == overflow;

		case 0xF:
			/* Less or equal */
			return zero || negative != overflow;
	}

	assert(cc_false);
	return cc_false;
}


/* Instruction Duration */

static void SingleOperandInstructionExecutionTimeWordOnly(Stuff* const stuff, const cc_u8f register_word, const cc_u8f memory_word)
{
	stuff->cycles_left_in_instruction += stuff->destination_decoded_address_mode.type == DECODED_ADDRESS_MODE_TYPE_REGISTER ? register_word : memory_word;
}

static void SingleOperandInstructionExecutionTimeLongwordOnly(Stuff* const stuff, const cc_u8f register_longword, const cc_u8f memory_longword)
{
	stuff->cycles_left_in_instruction += stuff->destination_decoded_address_mode.type == DECODED_ADDRESS_MODE_TYPE_REGISTER ? register_longword : memory_longword;
}

static void SingleOperandInstructionExecutionTime(Stuff* const stuff, const cc_u8f register_word, const cc_u8f memory_word, const cc_u8f register_longword, const cc_u8f memory_longword)
{
	if (stuff->operation_size == 4)
		SingleOperandInstructionExecutionTimeLongwordOnly(stuff, register_longword, memory_longword);
	else
		SingleOperandInstructionExecutionTimeWordOnly(stuff, register_word, memory_word);
}

static void SingleOperandInstructionExecutionTimeCommon(Stuff* const stuff)
{
	SingleOperandInstructionExecutionTime(stuff, 0, 4, 2, 8);
}

static void ShiftRotateInstructionExecutionTimeRegister(Stuff* const stuff, const cc_u8f count)
{
	stuff->cycles_left_in_instruction += 2 + 2 * count;

	if (stuff->operation_size == 4)
		stuff->cycles_left_in_instruction += 2;
}

static void ShiftRotateInstructionExecutionTimeMemory(Stuff* const stuff)
{
	stuff->cycles_left_in_instruction += 4;
}

static void ADDXSUBXExecutionTime(Stuff* const stuff)
{
	SingleOperandInstructionExecutionTime(stuff, 0, 2, 4, 6);
}

static void StandardInstructionExecutionTime(Stuff* const stuff)
{
	SingleOperandInstructionExecutionTimeCommon(stuff);

	switch (stuff->destination_decoded_address_mode.type)
	{
		case DECODED_ADDRESS_MODE_TYPE_REGISTER:
			if (stuff->operation_size == 4 && (stuff->source_decoded_address_mode.type == DECODED_ADDRESS_MODE_TYPE_REGISTER || (stuff->source_decoded_address_mode.type == DECODED_ADDRESS_MODE_TYPE_MEMORY && stuff->source_decoded_address_mode.data.memory.address == stuff->state->program_counter - 4)))
				stuff->cycles_left_in_instruction += 2;
			break;

		case DECODED_ADDRESS_MODE_TYPE_CONDITION_CODE_REGISTER:
		case DECODED_ADDRESS_MODE_TYPE_STATUS_REGISTER:
			stuff->cycles_left_in_instruction += 8;
			break;

		default:
			break;
	}
}

static void StandardInstructionExecutionTimeQuick(Stuff* const stuff)
{
	if (stuff->opcode.primary_address_mode == ADDRESS_MODE_ADDRESS_REGISTER || (stuff->operation_size == 4 && stuff->destination_decoded_address_mode.type == DECODED_ADDRESS_MODE_TYPE_REGISTER))
		stuff->cycles_left_in_instruction += 2;
}

static void LEAPEAInstructionExecutionTime(Stuff* const stuff)
{
	switch (stuff->opcode.primary_address_mode)
	{
		case ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_DISPLACEMENT:
			stuff->cycles_left_in_instruction += 4;
			break;

		case ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_INDEX:
			stuff->cycles_left_in_instruction += 8;
			break;

		case ADDRESS_MODE_SPECIAL:
			switch (stuff->opcode.primary_register)
			{
				case ADDRESS_MODE_REGISTER_SPECIAL_ABSOLUTE_SHORT:
				case ADDRESS_MODE_REGISTER_SPECIAL_PROGRAM_COUNTER_WITH_DISPLACEMENT:
					stuff->cycles_left_in_instruction += 4;
					break;

				case ADDRESS_MODE_REGISTER_SPECIAL_ABSOLUTE_LONG:
				case ADDRESS_MODE_REGISTER_SPECIAL_PROGRAM_COUNTER_WITH_INDEX:
					stuff->cycles_left_in_instruction += 8;
					break;

				default:
					break;
			}

			break;

		default:
			break;
	}
}

static void ABCDSBCDExecutionTime(Stuff* const stuff)
{
	stuff->cycles_left_in_instruction = (stuff->opcode.raw & 0x0008) != 0 ? 18 : 6;
}


/* Microcode Operations */

static void SupervisorCheck(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	/* Only allow this instruction in supervisor mode. */
	if ((state->status_register & STATUS_SUPERVISOR) == 0)
		Group1Or2Exception(stuff, 8);
}

static void SetSize_Byte(Stuff* const stuff)
{
	stuff->operation_size = 1;
}

static void SetSize_Word(Stuff* const stuff)
{
	stuff->operation_size = 2;
}

static void SetSize_Longword(Stuff* const stuff)
{
	stuff->operation_size = 4;
}

static void SetSize_LongwordRegisterByteMemory(Stuff* const stuff)
{
	stuff->operation_size = stuff->opcode.primary_address_mode == ADDRESS_MODE_DATA_REGISTER ? 4 : 1;
}

static void SetSize_Move(Stuff* const stuff)
{
	switch (stuff->opcode.raw & 0x3000)
	{
		case 0x0000: /* TODO: Verify this. */
		case 0x1000:
			stuff->operation_size = 1;
			break;

		case 0x2000:
			stuff->operation_size = 4; /* Yup, this isn't a typo. */
			break;

		case 0x3000:
			stuff->operation_size = 2;
			break;
	}
}

static void SetSize_Ext(Stuff* const stuff)
{
	stuff->operation_size = (stuff->opcode.raw & 0x0040) != 0 ? 4 : 2;
}

static void SetSize_Standard(Stuff* const stuff)
{
	static const cc_u8l sizes[] = {1, 2, 4, 4}; /* TODO: Verify that last one... */
	stuff->operation_size = sizes[stuff->opcode.bits_6_and_7];
}

static void SetMSBBitIndex(Stuff* const stuff)
{
	stuff->msb_bit_index = stuff->operation_size * 8 - 1;
}

static void DecodeSource_ImmediateData(Stuff* const stuff)
{
	/* Immediate value (any size). */
	DecodeAddressMode(stuff, &stuff->source_decoded_address_mode, stuff->operation_size, ADDRESS_MODE_SPECIAL, ADDRESS_MODE_REGISTER_SPECIAL_IMMEDIATE);
}

static void DecodeSource_DataRegisterSecondary(Stuff* const stuff)
{
	/* Secondary data register. */
	DecodeAddressMode(stuff, &stuff->source_decoded_address_mode, stuff->operation_size, ADDRESS_MODE_DATA_REGISTER, stuff->opcode.secondary_register);
}

static void DecodeSource_ImmediateDataByte(Stuff* const stuff)
{
	/* Immediate value (byte). */
	DecodeAddressMode(stuff, &stuff->source_decoded_address_mode, 1, ADDRESS_MODE_SPECIAL, ADDRESS_MODE_REGISTER_SPECIAL_IMMEDIATE);
}

static void DecodeSource_MemoryAddressPrimary(Stuff* const stuff)
{
	/* Memory address */
	DecodeAddressMode(stuff, &stuff->source_decoded_address_mode, 0, stuff->opcode.primary_address_mode, stuff->opcode.primary_register); /* 0 is a special value that means to obtain the address rather than the data at that address. */
}

static void DecodeSource_StatusRegister(Stuff* const stuff)
{
	stuff->source_decoded_address_mode.type = DECODED_ADDRESS_MODE_TYPE_STATUS_REGISTER;
}

static void DecodeSource_ImmediateDataWord(Stuff* const stuff)
{
	/* Immediate value (word). */
	DecodeAddressMode(stuff, &stuff->source_decoded_address_mode, 2, ADDRESS_MODE_SPECIAL, ADDRESS_MODE_REGISTER_SPECIAL_IMMEDIATE);
}

static void DecodeSource_BCDX(Stuff* const stuff)
{
	DecodeAddressMode(stuff, &stuff->source_decoded_address_mode, stuff->operation_size, (stuff->opcode.raw & 0x0008) != 0 ? ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_PREDECREMENT : ADDRESS_MODE_DATA_REGISTER, stuff->opcode.primary_register);
}

static void DecodeSource_DataRegisterSecondaryOrPrimaryAddressMode(Stuff* const stuff)
{
	/* Primary address mode or secondary data register, based on direction bit. */
	DecodeAddressMode(stuff, &stuff->source_decoded_address_mode, stuff->operation_size, stuff->opcode.bit_8 ? ADDRESS_MODE_DATA_REGISTER : stuff->opcode.primary_address_mode, stuff->opcode.bit_8 ? stuff->opcode.secondary_register : stuff->opcode.primary_register);
}

static void DecodeSource_PrimaryAddressModeSized(Stuff* const stuff)
{
	/* Word or longword based on bit 8. */
	DecodeAddressMode(stuff, &stuff->source_decoded_address_mode, stuff->opcode.bit_8 ? 4 : 2, stuff->opcode.primary_address_mode, stuff->opcode.primary_register);
}

static void DecodeSource_AddressRegisterPrimaryPostIncrement(Stuff* const stuff)
{
	DecodeAddressMode(stuff, &stuff->source_decoded_address_mode, stuff->operation_size, ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_POSTINCREMENT, stuff->opcode.primary_register);
}

static void DecodeSource_PrimaryAddressMode(Stuff* const stuff)
{
	/* Primary address mode. */
	DecodeAddressMode(stuff, &stuff->source_decoded_address_mode, stuff->operation_size, stuff->opcode.primary_address_mode, stuff->opcode.primary_register);
}

static void DecodeSource_PrimaryAddressModeWord(Stuff* const stuff)
{
	/* Primary address mode, hardcoded to word-size. */
	DecodeAddressMode(stuff, &stuff->source_decoded_address_mode, 2, stuff->opcode.primary_address_mode, stuff->opcode.primary_register);
}

static void DecodeDestination_DataRegisterPrimary(Stuff* const stuff)
{
	/* Data register (primary) */
	DecodeAddressMode(stuff, &stuff->destination_decoded_address_mode, stuff->operation_size, ADDRESS_MODE_DATA_REGISTER, stuff->opcode.primary_register);
}

static void DecodeDestination_DataRegisterSecondary(Stuff* const stuff)
{
	/* Data register (secondary) */
	DecodeAddressMode(stuff, &stuff->destination_decoded_address_mode, stuff->operation_size, ADDRESS_MODE_DATA_REGISTER, stuff->opcode.secondary_register);
}

static void DecodeDestination_AddressRegisterSecondary(Stuff* const stuff)
{
	/* Address register (secondary) */
	DecodeAddressMode(stuff, &stuff->destination_decoded_address_mode, stuff->operation_size, ADDRESS_MODE_ADDRESS_REGISTER, stuff->opcode.secondary_register);
}

static void DecodeDestination_SecondaryAddressMode(Stuff* const stuff)
{
	/* Secondary address mode */
	DecodeAddressMode(stuff, &stuff->destination_decoded_address_mode, stuff->operation_size, stuff->opcode.secondary_address_mode, stuff->opcode.secondary_register);
}

static void DecodeDestination_BCDX(Stuff* const stuff)
{
	DecodeAddressMode(stuff, &stuff->destination_decoded_address_mode, stuff->operation_size, (stuff->opcode.raw & 0x0008) != 0 ? ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_PREDECREMENT : ADDRESS_MODE_DATA_REGISTER, stuff->opcode.secondary_register);
}

static void DecodeDestination_DataRegisterSecondaryOrPrimaryAddressMode(Stuff* const stuff)
{
	/* Primary address mode or secondary data register, based on direction bit */
	DecodeAddressMode(stuff, &stuff->destination_decoded_address_mode, stuff->operation_size, stuff->opcode.bit_8 ? stuff->opcode.primary_address_mode : ADDRESS_MODE_DATA_REGISTER, stuff->opcode.bit_8 ? stuff->opcode.primary_register : stuff->opcode.secondary_register);
}

static void DecodeDestination_AddressRegisterSecondaryFull(Stuff* const stuff)
{
	/* Full secondary address register */
	DecodeAddressMode(stuff, &stuff->destination_decoded_address_mode, 4, ADDRESS_MODE_ADDRESS_REGISTER, stuff->opcode.secondary_register);
}

static void DecodeDestination_AddressRegisterSecondaryPostIncrement(Stuff* const stuff)
{
	DecodeAddressMode(stuff, &stuff->destination_decoded_address_mode, stuff->operation_size, ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_POSTINCREMENT, stuff->opcode.secondary_register);
}

static void DecodeDestination_PrimaryAddressMode(Stuff* const stuff)
{
	/* Using primary address mode */
	DecodeAddressMode(stuff, &stuff->destination_decoded_address_mode, stuff->operation_size, stuff->opcode.primary_address_mode, stuff->opcode.primary_register);
}

static void DecodeDestination_ConditionCodeRegister(Stuff* const stuff)
{
	stuff->destination_decoded_address_mode.type = DECODED_ADDRESS_MODE_TYPE_CONDITION_CODE_REGISTER;
}

static void DecodeDestination_StatusRegister(Stuff* const stuff)
{
	stuff->destination_decoded_address_mode.type = DECODED_ADDRESS_MODE_TYPE_STATUS_REGISTER;
}

static void DecodeDestination_MOVEM(Stuff* const stuff)
{
	DecodeAddressMode(stuff, &stuff->destination_decoded_address_mode, 0, stuff->opcode.primary_address_mode, stuff->opcode.primary_register); /* 0 is a special value that means to obtain the address rather than the data at that address. */
}

static void DecodeDestination_MOVEP(Stuff* const stuff)
{
	DecodeAddressMode(stuff, &stuff->destination_decoded_address_mode, 0, ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_DISPLACEMENT, stuff->opcode.primary_register); /* 0 is a special value that means to obtain the address rather than the data at that address. */
}

static void ReadSource(Stuff* const stuff)
{
	stuff->source_value = GetValueUsingDecodedAddressMode(stuff, &stuff->source_decoded_address_mode);
}

static void ReadDestination(Stuff* const stuff)
{
	stuff->destination_value = GetValueUsingDecodedAddressMode(stuff, &stuff->destination_decoded_address_mode);
}

static void WriteDestination(Stuff* const stuff)
{
	SetValueUsingDecodedAddressMode(stuff, &stuff->destination_decoded_address_mode, stuff->result_value);
}

static void Carry_StandardCarry(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	state->status_register &= ~CONDITION_CODE_CARRY;
	state->status_register |= (((stuff->source_value & stuff->destination_value) | ((stuff->source_value | stuff->destination_value) & ~stuff->result_value)) >> (stuff->msb_bit_index - CONDITION_CODE_CARRY_BIT)) & CONDITION_CODE_CARRY;
}

static void Carry_StandardBorrow(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	state->status_register &= ~CONDITION_CODE_CARRY;
	state->status_register |= (((stuff->source_value & ~stuff->destination_value) | ((stuff->source_value | ~stuff->destination_value) & stuff->result_value)) >> (stuff->msb_bit_index - CONDITION_CODE_CARRY_BIT)) & CONDITION_CODE_CARRY;
}

static void Carry_NEG(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	state->status_register &= ~CONDITION_CODE_CARRY;
	state->status_register |= ((stuff->destination_value | stuff->result_value) >> (stuff->msb_bit_index - CONDITION_CODE_CARRY_BIT)) & CONDITION_CODE_CARRY;
}

static void Carry_Clear(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	state->status_register &= ~CONDITION_CODE_CARRY;
}

static void Overflow_ADD(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	state->status_register &= ~CONDITION_CODE_OVERFLOW;
	state->status_register |= (((stuff->source_value & stuff->destination_value & ~stuff->result_value) | (~stuff->source_value & ~stuff->destination_value & stuff->result_value)) >> (stuff->msb_bit_index - CONDITION_CODE_OVERFLOW_BIT)) & CONDITION_CODE_OVERFLOW;
}

static void Overflow_SUB(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	state->status_register &= ~CONDITION_CODE_OVERFLOW;
	state->status_register |= (((~stuff->source_value & stuff->destination_value & ~stuff->result_value) | (stuff->source_value & ~stuff->destination_value & stuff->result_value)) >> (stuff->msb_bit_index - CONDITION_CODE_OVERFLOW_BIT)) & CONDITION_CODE_OVERFLOW;
}

static void Overflow_NEG(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	state->status_register &= ~CONDITION_CODE_OVERFLOW;
	state->status_register |= ((stuff->destination_value & stuff->result_value) >> (stuff->msb_bit_index - CONDITION_CODE_OVERFLOW_BIT)) & CONDITION_CODE_OVERFLOW;
}

static void Overflow_Clear(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	state->status_register &= ~CONDITION_CODE_OVERFLOW;
}

static void Zero_ClearIfNonZeroUnaffectedOtherwise(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	/* Cleared if the result is nonzero; unchanged otherwise */
	state->status_register &= ~CONDITION_CODE_ZERO | (0 - ((stuff->result_value & (0xFFFFFFFF >> (32 - stuff->msb_bit_index - 1))) == 0));
}

static void Zero_SetIfZeroClearOtherwise(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	/* Standard behaviour: set if result is zero; clear otherwise */
	state->status_register &= ~CONDITION_CODE_ZERO;
	state->status_register |= CONDITION_CODE_ZERO & (0 - ((stuff->result_value & (0xFFFFFFFF >> (32 - stuff->msb_bit_index - 1))) == 0));
}

static void Negative_SetIfNegativeClearOtherwise(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	/* Standard behaviour: set if result value is negative; clear otherwise */
	state->status_register &= ~CONDITION_CODE_NEGATIVE;
	state->status_register |= CONDITION_CODE_NEGATIVE & (stuff->result_value >> (stuff->msb_bit_index - CONDITION_CODE_NEGATIVE_BIT)) & CONDITION_CODE_NEGATIVE;
}

static void Extend_SetToCarry(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	/* Standard behaviour: set to CARRY */
	state->status_register &= ~CONDITION_CODE_EXTEND;
	state->status_register |= CONDITION_CODE_EXTEND & (0 - ((state->status_register & CONDITION_CODE_CARRY) != 0));
}

static void Action_OR(Stuff* const stuff)
{
	stuff->result_value = stuff->destination_value | stuff->source_value;

	StandardInstructionExecutionTime(stuff);
}

static void Action_AND(Stuff* const stuff)
{
	stuff->result_value = stuff->destination_value & stuff->source_value;

	StandardInstructionExecutionTime(stuff);
}

static void Action_SUBCommon(Stuff* const stuff)
{
	stuff->result_value = stuff->destination_value - stuff->source_value;
}

static void Action_CMP(Stuff* const stuff)
{
	Action_SUBCommon(stuff);

	stuff->cycles_left_in_instruction += stuff->operation_size == 4 ? 2 : 0;
}

static void Action_CMPI(Stuff* const stuff)
{
	Action_SUBCommon(stuff);

	if (stuff->operation_size == 4 && stuff->destination_decoded_address_mode.type == DECODED_ADDRESS_MODE_TYPE_REGISTER)
		stuff->cycles_left_in_instruction += 2;
}

static void Action_CMPM(Stuff* const stuff)
{
	Action_SUBCommon(stuff);
}

static void Action_SUB(Stuff* const stuff)
{
	Action_SUBCommon(stuff);

	StandardInstructionExecutionTime(stuff);
}


static void Action_ADDASUBACommon(Stuff* const stuff)
{
	if (!stuff->opcode.bit_8)
	{
		stuff->source_value = CC_SIGN_EXTEND_ULONG(15, stuff->source_value);

		if (stuff->source_decoded_address_mode.type != DECODED_ADDRESS_MODE_TYPE_REGISTER)
			stuff->cycles_left_in_instruction += 2;
	}
}

static void Action_CMPA(Stuff* const stuff)
{
	if (!stuff->opcode.bit_8)
		stuff->source_value = CC_SIGN_EXTEND_ULONG(15, stuff->source_value);

	Action_CMP(stuff);
}

static void Action_SUBA(Stuff* const stuff)
{
	Action_ADDASUBACommon(stuff);
	Action_SUB(stuff);
}

static void Action_SUBQ(Stuff* const stuff)
{
	stuff->source_value = ((stuff->opcode.secondary_register - 1u) & 7u) + 1u; /* A little math trick to turn 0 into 8. */
	Action_SUB(stuff);

	StandardInstructionExecutionTimeQuick(stuff);
}

static void Action_ADD(Stuff* const stuff)
{
	stuff->result_value = stuff->destination_value + stuff->source_value;

	StandardInstructionExecutionTime(stuff);
}

static void Action_ADDA(Stuff* const stuff)
{
	Action_ADDASUBACommon(stuff);
	Action_ADD(stuff);
}

static void Action_ADDQ(Stuff* const stuff)
{
	stuff->source_value = ((stuff->opcode.secondary_register - 1u) & 7u) + 1u; /* A little math trick to turn 0 into 8. */
	Action_ADD(stuff);

	StandardInstructionExecutionTimeQuick(stuff);
}

static void Action_EOR(Stuff* const stuff)
{
	stuff->result_value = stuff->destination_value ^ stuff->source_value;

	StandardInstructionExecutionTime(stuff);
}

static void Action_Bxxx(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	/* Modulo the source value */
	stuff->source_value &= stuff->msb_bit_index;

	/* Set the zero flag to the specified bit */
	state->status_register &= ~CONDITION_CODE_ZERO;
	state->status_register |= CONDITION_CODE_ZERO & (0 - ((stuff->destination_value & (1ul << stuff->source_value)) == 0));
}

static void Action_BTST(Stuff* const stuff)
{
	Action_Bxxx(stuff);

	if (stuff->operation_size == 4 || (stuff->opcode.primary_address_mode == ADDRESS_MODE_SPECIAL && stuff->opcode.primary_register == ADDRESS_MODE_REGISTER_SPECIAL_IMMEDIATE))
		stuff->cycles_left_in_instruction += 2;

	/* TODO: 'BTST d0,#' timing. */
}

static void Action_BCHG(Stuff* const stuff)
{
	Action_Bxxx(stuff);
	stuff->result_value = stuff->destination_value ^ (1ul << stuff->source_value);

	stuff->cycles_left_in_instruction += 4;

	if (stuff->operation_size == 4 && stuff->source_value < 16)
		stuff->cycles_left_in_instruction -= 2;
}

static void Action_BCLR(Stuff* const stuff)
{
	Action_Bxxx(stuff);
	stuff->result_value = stuff->destination_value & ~(1ul << stuff->source_value);

	stuff->cycles_left_in_instruction += 4;

	if (stuff->operation_size == 4 && stuff->source_value >= 16)
		stuff->cycles_left_in_instruction += 2;
}

static void Action_BSET(Stuff* const stuff)
{
	Action_Bxxx(stuff);
	stuff->result_value = stuff->destination_value | (1ul << stuff->source_value);

	stuff->cycles_left_in_instruction += 4;

	if (stuff->operation_size == 4 && stuff->source_value < 16)
		stuff->cycles_left_in_instruction -= 2;
}

static void Action_MOVEP(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	switch (stuff->opcode.bits_6_and_7)
	{
		case 0:
			/* Memory to register (word) */
			state->data_registers[stuff->opcode.secondary_register] &= ~0xFFFFul;
			state->data_registers[stuff->opcode.secondary_register] |= ReadByte(stuff, stuff->destination_value + 2 * 0) << 8 * 1;
			state->data_registers[stuff->opcode.secondary_register] |= ReadByte(stuff, stuff->destination_value + 2 * 1) << 8 * 0;

			stuff->cycles_left_in_instruction += 4;
			break;

		case 1:
			/* Memory to register (longword) */
			state->data_registers[stuff->opcode.secondary_register] = 0;
			state->data_registers[stuff->opcode.secondary_register] |= ReadByte(stuff, stuff->destination_value + 2 * 0) << 8 * 3;
			state->data_registers[stuff->opcode.secondary_register] |= ReadByte(stuff, stuff->destination_value + 2 * 1) << 8 * 2;
			state->data_registers[stuff->opcode.secondary_register] |= ReadByte(stuff, stuff->destination_value + 2 * 2) << 8 * 1;
			state->data_registers[stuff->opcode.secondary_register] |= ReadByte(stuff, stuff->destination_value + 2 * 3) << 8 * 0;

			stuff->cycles_left_in_instruction += 12;
			break;

		case 2:
			/* Register to memory (word) */
			WriteByte(stuff, stuff->destination_value + 2 * 0, (state->data_registers[stuff->opcode.secondary_register] >> 8 * 1) & 0xFF);
			WriteByte(stuff, stuff->destination_value + 2 * 1, (state->data_registers[stuff->opcode.secondary_register] >> 8 * 0) & 0xFF);

			stuff->cycles_left_in_instruction += 4;
			break;

		case 3:
			/* Register to memory (longword) */
			WriteByte(stuff, stuff->destination_value + 2 * 0, (state->data_registers[stuff->opcode.secondary_register] >> 8 * 3) & 0xFF);
			WriteByte(stuff, stuff->destination_value + 2 * 1, (state->data_registers[stuff->opcode.secondary_register] >> 8 * 2) & 0xFF);
			WriteByte(stuff, stuff->destination_value + 2 * 2, (state->data_registers[stuff->opcode.secondary_register] >> 8 * 1) & 0xFF);
			WriteByte(stuff, stuff->destination_value + 2 * 3, (state->data_registers[stuff->opcode.secondary_register] >> 8 * 0) & 0xFF);

			stuff->cycles_left_in_instruction += 12;
			break;
	}
}

static void Action_MOVEA(Stuff* const stuff)
{
	stuff->result_value = CC_SIGN_EXTEND_ULONG(stuff->msb_bit_index, stuff->source_value);
}

static void Action_MOVECommon(Stuff* const stuff)
{
	stuff->result_value = stuff->source_value;
}

static void Action_MOVE(Stuff* const stuff)
{
	Action_MOVECommon(stuff);

	if (stuff->source_decoded_address_mode.type == DECODED_ADDRESS_MODE_TYPE_STATUS_REGISTER)
	{
		if (stuff->destination_decoded_address_mode.type == DECODED_ADDRESS_MODE_TYPE_REGISTER)
			stuff->cycles_left_in_instruction += 2;
		else
			stuff->cycles_left_in_instruction += 4;
	}
	else if (stuff->destination_decoded_address_mode.type == DECODED_ADDRESS_MODE_TYPE_STATUS_REGISTER || stuff->destination_decoded_address_mode.type == DECODED_ADDRESS_MODE_TYPE_CONDITION_CODE_REGISTER)
	{
		stuff->cycles_left_in_instruction += 8;
	}
	else if (stuff->opcode.secondary_address_mode == ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_PREDECREMENT)
	{
		stuff->cycles_left_in_instruction -= 2;
	}
}

static void Action_LINK(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	/* Push address register to stack */
	const cc_u32l address_register_contents = state->address_registers[stuff->opcode.primary_register];

	DecrementAddressRegister(state, 7, 4);
	WriteLongWordBackwards(stuff, state->address_registers[7], address_register_contents);

	/* Copy stack pointer to address register */
	state->address_registers[stuff->opcode.primary_register] = state->address_registers[7];

	/* Offset the stack pointer by the immediate value */
	IncrementAddressRegister(state, 7, CC_SIGN_EXTEND_ULONG(15, stuff->source_value));

	stuff->cycles_left_in_instruction += 8;
}

static void Action_UNLK(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	const cc_u32l address_register_contents = state->address_registers[stuff->opcode.primary_register];
	const cc_u32l value = ReadLongWord(stuff, address_register_contents);

	state->address_registers[7] = address_register_contents;
	IncrementAddressRegister(state, 7, 4);
	state->address_registers[stuff->opcode.primary_register] = value;

	stuff->cycles_left_in_instruction += 8;
}

static void Action_NEGX(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	stuff->result_value = 0 - stuff->destination_value - ((state->status_register & CONDITION_CODE_EXTEND) != 0 ? 1 : 0);
	SingleOperandInstructionExecutionTimeCommon(stuff);
}

static void Action_CLR(Stuff* const stuff)
{
	stuff->result_value = 0;
	SingleOperandInstructionExecutionTimeCommon(stuff);
}

static void Action_NEG(Stuff* const stuff)
{
	stuff->result_value = 0 - stuff->destination_value;
	SingleOperandInstructionExecutionTimeCommon(stuff);
}

static void Action_NOT(Stuff* const stuff)
{
	stuff->result_value = ~stuff->destination_value;
	SingleOperandInstructionExecutionTimeCommon(stuff);
}

static void Action_EXT(Stuff* const stuff)
{
	stuff->result_value = CC_SIGN_EXTEND_ULONG((stuff->opcode.raw & 0x0040) != 0 ? 15 : 7, stuff->destination_value);
}

static void Action_SWAP(Stuff* const stuff)
{
	stuff->result_value = ((stuff->destination_value & 0x0000FFFF) << 16) | ((stuff->destination_value & 0xFFFF0000) >> 16);
}

static void Action_PEA(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	DecrementAddressRegister(state, 7, 4);
	WriteLongWordBackwards(stuff, state->address_registers[7], stuff->source_value);

	stuff->cycles_left_in_instruction = 12;
	LEAPEAInstructionExecutionTime(stuff);
}

static void Action_ILLEGAL(Stuff* const stuff)
{
	/* Illegal instruction. */
	Group1Or2Exception(stuff, 4);
}

static void Action_TAS(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	/* TODO - This instruction doesn't work properly on memory on the Mega Drive */
	state->status_register &= ~(CONDITION_CODE_NEGATIVE | CONDITION_CODE_ZERO);
	state->status_register |= CONDITION_CODE_NEGATIVE & (0 - ((stuff->destination_value & 0x80) != 0));
	state->status_register |= CONDITION_CODE_ZERO & (0 - (stuff->destination_value == 0));

	stuff->result_value = stuff->destination_value | 0x80;

	SingleOperandInstructionExecutionTimeWordOnly(stuff, 0, 6);
}

static void Action_TRAP(Stuff* const stuff)
{
	stuff->source_value = stuff->opcode.raw & 0xF;
	DoInterrupt(stuff, 32 + stuff->source_value);
}

static void Action_MOVE_USP(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	if ((stuff->opcode.raw & 8) != 0)
		state->address_registers[stuff->opcode.primary_register] = state->user_stack_pointer;
	else
		state->user_stack_pointer = state->address_registers[stuff->opcode.primary_register];
}

#define UNIMPLEMENTED_INSTRUCTION(instruction) Clown68000_PrintError("Unimplemented instruction " instruction " used at 0x%" CC_PRIXLEAST32, stuff->state->program_counter)

static void ProgramCounterChanged(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	const cc_u32f program_counter = state->program_counter;

	/* TODO: Instruction prefetch is normally what causes this, but that is not yet emulated so we have to do this manually instead. */
	if ((program_counter & 1) != 0)
		Group0Exception(stuff, 3, program_counter, cc_true);
}

static void SetStatusRegister(Stuff* const stuff, const cc_u16f value)
{
	Clown68000_State* const state = stuff->state;

	SetSupervisorMode(state, (value & STATUS_SUPERVISOR) != 0);
	/* Keep only the bits that are actually used. */
	state->status_register = value & STATUS_REGISTER_MASK;
}

static void Action_RESET(Stuff* const stuff)
{
	/* TODO */
	UNIMPLEMENTED_INSTRUCTION("RESET");

	stuff->cycles_left_in_instruction += 128;
}

static void Action_STOP(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	SetStatusRegister(stuff, stuff->source_value);
	state->stopped = cc_true;

	stuff->cycles_left_in_instruction -= 4;
}

static void Action_RTE(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	const cc_u16f new_status = ReadWord(stuff, state->address_registers[7]) & STATUS_REGISTER_MASK;

	IncrementAddressRegister(state, 7, 2);
	state->program_counter = ReadLongWord(stuff, state->address_registers[7]);
	IncrementAddressRegister(state, 7, 4);

	SetStatusRegister(stuff, new_status);
	ProgramCounterChanged(stuff);

	stuff->cycles_left_in_instruction += 16;
}

static void Action_RTS(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	state->program_counter = ReadLongWord(stuff, state->address_registers[7]);
	IncrementAddressRegister(state, 7, 4);

	ProgramCounterChanged(stuff);

	stuff->cycles_left_in_instruction += 12;
}

static void Action_TRAPV(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	if ((state->status_register & CONDITION_CODE_OVERFLOW) != 0)
		DoInterrupt(stuff, 7);
}

static void Action_RTR(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	state->status_register &= ~CONDITION_CODE_REGISTER_MASK;
	state->status_register |= ReadByte(stuff, state->address_registers[7] + 1) & CONDITION_CODE_REGISTER_MASK;
	IncrementAddressRegister(state, 7, 2);
	state->program_counter = ReadLongWord(stuff, state->address_registers[7]);
	IncrementAddressRegister(state, 7, 4);

	ProgramCounterChanged(stuff);

	stuff->cycles_left_in_instruction += 16;
}

static void Action_JMP(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	state->program_counter = stuff->source_value;

	ProgramCounterChanged(stuff);

	stuff->cycles_left_in_instruction = 8;

	switch (stuff->opcode.primary_address_mode)
	{
		case ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_DISPLACEMENT:
			stuff->cycles_left_in_instruction += 2;
			break;

		case ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_INDEX:
			stuff->cycles_left_in_instruction += 6;
			break;

		case ADDRESS_MODE_SPECIAL:
			switch (stuff->opcode.primary_register)
			{
				case ADDRESS_MODE_REGISTER_SPECIAL_ABSOLUTE_SHORT:
				case ADDRESS_MODE_REGISTER_SPECIAL_PROGRAM_COUNTER_WITH_DISPLACEMENT:
					stuff->cycles_left_in_instruction += 2;
					break;

				case ADDRESS_MODE_REGISTER_SPECIAL_ABSOLUTE_LONG:
					stuff->cycles_left_in_instruction += 4;
					break;

				case ADDRESS_MODE_REGISTER_SPECIAL_PROGRAM_COUNTER_WITH_INDEX:
					stuff->cycles_left_in_instruction += 6;
					break;

				default:
					break;
			}

			break;

		default:
			break;
	}
}

static void Action_JSR(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	const cc_u32f program_counter = state->program_counter;

	/* According to SingleStepTests, this needs to be done first. */
	Action_JMP(stuff);
	DecrementAddressRegister(state, 7, 4);
	WriteLongWordBackwards(stuff, state->address_registers[7], program_counter);

	stuff->cycles_left_in_instruction += 8;
}

static void Action_LEA(Stuff* const stuff)
{
	Action_MOVECommon(stuff);

	stuff->cycles_left_in_instruction = 4;
	LEAPEAInstructionExecutionTime(stuff);
}

static void Action_TST(Stuff* const stuff)
{
	Action_MOVECommon(stuff);
}

static void Action_MOVEM(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	/* Hot damn is this a mess */
	cc_u32f memory_address = stuff->destination_value; /* TODO: Maybe get rid of this alias? */
	cc_u16f i;
	cc_u16f bitfield;

	int address_delta;
	void (*write_function)(Stuff *stuff, cc_u32f address, cc_u32f value);

	const cc_bool memory_to_register = (stuff->opcode.raw & 0x0400) != 0;
	const cc_bool is_longword = (stuff->opcode.raw & 0x0040) != 0;
	const unsigned int cycle_delta = is_longword ? 8 : 4;

	stuff->cycles_left_in_instruction = 8;

	if (memory_to_register)
		stuff->cycles_left_in_instruction += 4;

	switch (stuff->opcode.primary_address_mode)
	{
		case ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_DISPLACEMENT:
			stuff->cycles_left_in_instruction += 4;
			break;

		case ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_INDEX:
			stuff->cycles_left_in_instruction += 6;
			break;

		case ADDRESS_MODE_SPECIAL:
			switch (stuff->opcode.primary_register)
			{
				case ADDRESS_MODE_REGISTER_SPECIAL_PROGRAM_COUNTER_WITH_DISPLACEMENT:
					stuff->cycles_left_in_instruction += 4;
					break;

				case ADDRESS_MODE_REGISTER_SPECIAL_PROGRAM_COUNTER_WITH_INDEX:
					stuff->cycles_left_in_instruction += 6;
					break;

				case ADDRESS_MODE_REGISTER_SPECIAL_ABSOLUTE_SHORT:
					stuff->cycles_left_in_instruction += 4;
					break;

				case ADDRESS_MODE_REGISTER_SPECIAL_ABSOLUTE_LONG:
					stuff->cycles_left_in_instruction += 8;
					break;

				default:
					break;
			}

			break;

		default:
			break;
	}

	if (stuff->opcode.primary_address_mode == ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_PREDECREMENT)
	{
		if (is_longword)
		{
			address_delta = -4;
			write_function = WriteLongWordBackwards;
		}
		else
		{
			address_delta = -2;
			write_function = WriteWord;
		}
	}
	else
	{
		if (is_longword)
		{
			address_delta = 4;
			write_function = WriteLongWord;
		}
		else
		{
			address_delta = 2;
			write_function = WriteWord;
		}
	}

	bitfield = stuff->source_value;

	/* First group of registers */
	for (i = 0; i < 8; ++i)
	{
		if ((bitfield & 1) != 0)
		{
			stuff->cycles_left_in_instruction += cycle_delta;

			if (memory_to_register)
			{
				/* Memory to register */
				if (is_longword)
					state->data_registers[i] = ReadLongWord(stuff, memory_address);
				else
					state->data_registers[i] = CC_SIGN_EXTEND_ULONG(15, ReadWord(stuff, memory_address));
			}
			else
			{
				/* Register to memory */
				if (stuff->opcode.primary_address_mode == ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_PREDECREMENT)
					write_function(stuff, memory_address + address_delta, state->address_registers[7 - i]);
				else
					write_function(stuff, memory_address, state->data_registers[i]);
			}

			memory_address += address_delta;
			memory_address &= 0xFFFFFFFF;
		}

		bitfield >>= 1;
	}

	/* Second group of registers */
	for (i = 0; i < 8; ++i)
	{
		if ((bitfield & 1) != 0)
		{
			stuff->cycles_left_in_instruction += cycle_delta;

			if (memory_to_register)
			{
				/* Memory to register */
				if (is_longword)
					state->address_registers[i] = ReadLongWord(stuff, memory_address);
				else
					state->address_registers[i] = CC_SIGN_EXTEND_ULONG(15, ReadWord(stuff, memory_address)) & 0xFFFFFFFF;
			}
			else
			{
				/* Register to memory */
				if (stuff->opcode.primary_address_mode == ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_PREDECREMENT)
					write_function(stuff, memory_address + address_delta, state->data_registers[7 - i]);
				else
					write_function(stuff, memory_address, state->address_registers[i]);
			}

			memory_address += address_delta;
			memory_address &= 0xFFFFFFFF;
		}

		bitfield >>= 1;
	}

	if (stuff->opcode.primary_address_mode == ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_PREDECREMENT || stuff->opcode.primary_address_mode == ADDRESS_MODE_ADDRESS_REGISTER_INDIRECT_WITH_POSTINCREMENT)
		state->address_registers[stuff->opcode.primary_register] = memory_address;
}

static void Action_CHK(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	const cc_u32f value = state->data_registers[stuff->opcode.secondary_register] & 0xFFFF;

	state->status_register &= ~(CONDITION_CODE_NEGATIVE | CONDITION_CODE_CARRY | CONDITION_CODE_OVERFLOW | CONDITION_CODE_ZERO);

	if ((value & 0x8000) != 0)
	{
		/* Value is smaller than 0. */
		state->status_register |= CONDITION_CODE_NEGATIVE;
		DoInterrupt(stuff, 6);
	}
	else if ((value ^ 0x8000) > (stuff->source_value ^ 0x8000))
	{
		/* Value is greater than upper bound. */
		DoInterrupt(stuff, 6);
	}

	stuff->cycles_left_in_instruction += 6;
}

static void Action_SCC(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	if (IsOpcodeConditionTrue(state, stuff->opcode.raw))
	{
		stuff->result_value = 0xFF;
		SingleOperandInstructionExecutionTimeWordOnly(stuff, 2, 4);
	}
	else
	{
		stuff->result_value = 0;
		SingleOperandInstructionExecutionTimeWordOnly(stuff, 0, 4);
	}
}

static void Action_BRA_SHORT(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	IncrementProgramCounter(state, CC_SIGN_EXTEND_ULONG(7, stuff->opcode.raw));

	ProgramCounterChanged(stuff);

	stuff->cycles_left_in_instruction += 6;
}

static void Action_BRA_WORD(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	IncrementProgramCounter(state, CC_SIGN_EXTEND_ULONG(15, stuff->source_value) - 2);

	ProgramCounterChanged(stuff);

	stuff->cycles_left_in_instruction += 2;
}

static void Action_BSR_SHORT(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	DecrementAddressRegister(state, 7, 4);
	WriteLongWordBackwards(stuff, state->address_registers[7], state->program_counter);
	Action_BRA_SHORT(stuff);

	stuff->cycles_left_in_instruction += 8;
}

static void Action_BSR_WORD(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	DecrementAddressRegister(state, 7, 4);
	WriteLongWordBackwards(stuff, state->address_registers[7], state->program_counter);
	Action_BRA_WORD(stuff);

	stuff->cycles_left_in_instruction += 8;
}

static void Action_BCC_SHORT(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	if (IsOpcodeConditionTrue(state, stuff->opcode.raw))
		Action_BRA_SHORT(stuff);
	else
		stuff->cycles_left_in_instruction += 4;
}

static void Action_BCC_WORD(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	if (IsOpcodeConditionTrue(state, stuff->opcode.raw))
		Action_BRA_WORD(stuff);
	else
		stuff->cycles_left_in_instruction += 4;
}

static void Action_DBCC(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	if (!IsOpcodeConditionTrue(state, stuff->opcode.raw))
	{
		cc_u16f loop_counter = state->data_registers[stuff->opcode.primary_register] & 0xFFFF;

		if (loop_counter-- != 0)
			Action_BRA_WORD(stuff);
		else
			stuff->cycles_left_in_instruction += 6;

		state->data_registers[stuff->opcode.primary_register] &= ~0xFFFFul;
		state->data_registers[stuff->opcode.primary_register] |= loop_counter & 0xFFFF;
	}
	else
	{
		stuff->cycles_left_in_instruction += 4;
	}
}

static void Action_MOVEQ(Stuff* const stuff)
{
	stuff->result_value = CC_SIGN_EXTEND_ULONG(7, stuff->opcode.raw);
}

static cc_u16f CountBitsSet(cc_u16f value)
{
	/* https://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetKernighan */
	cc_u16f total_bits_set;

	for (total_bits_set = 0; value != 0; ++total_bits_set)
		value &= value - 1;

	return total_bits_set;
}

static void Action_DIVCommon(Stuff* const stuff, const cc_bool is_signed)
{
	Clown68000_State* const state = stuff->state;

	state->status_register &= ~CONDITION_CODE_CARRY;

	if (stuff->source_value == 0)
	{
		/* TODO: This hack is needed for the validator. Is the validator actually correct? */
		state->status_register &= ~(CONDITION_CODE_NEGATIVE | CONDITION_CODE_ZERO | CONDITION_CODE_OVERFLOW);

		stuff->result_value = stuff->destination_value;

		DoInterrupt(stuff, 5);
	}
	else
	{
		const cc_bool source_is_negative = is_signed && (stuff->source_value & 0x8000) != 0;
		const cc_bool destination_is_negative = is_signed && (stuff->destination_value & 0x80000000) != 0;
		const cc_bool result_is_negative = source_is_negative != destination_is_negative;

		const cc_u32f absolute_source_value = source_is_negative ? 0 - CC_SIGN_EXTEND_ULONG(15, stuff->source_value) : stuff->source_value;
		const cc_u32f absolute_destination_value = destination_is_negative ? 0 - CC_SIGN_EXTEND_ULONG(31, stuff->destination_value) : stuff->destination_value;

		stuff->cycles_left_in_instruction += 6;

		if (is_signed)
			stuff->cycles_left_in_instruction += destination_is_negative ? 8 : 6;

		/* Unsigned overflow detection. */
		if (absolute_source_value >= (absolute_destination_value >> 16))
		{
			const cc_u32f absolute_quotient = absolute_destination_value / absolute_source_value;

			/* Compute duration. */
			/* https://gendev.spritesmind.net/forum/viewtopic.php?p=35569#p35569 */
			if (is_signed)
			{
				stuff->cycles_left_in_instruction += 104;

				if (source_is_negative)
					stuff->cycles_left_in_instruction += 2;
				else if (destination_is_negative)
					stuff->cycles_left_in_instruction += 4;

				stuff->cycles_left_in_instruction += (15 - CountBitsSet(absolute_quotient >> 1)) * 2;
			}
			else
			{
				const cc_u32f shifted_divisor = (absolute_source_value & 0xFFFF) << 16;

				cc_u32f working_dividend = absolute_destination_value;
				cc_u8f i;

				stuff->cycles_left_in_instruction += 66;

				for (i = 0; i < 15; ++i)
				{
					const cc_bool high_bit_set = (working_dividend & 0x80000000) != 0;

					working_dividend <<= 1;

					if (!high_bit_set)
					{
						stuff->cycles_left_in_instruction += 2;

						if (working_dividend < shifted_divisor)
						{
							stuff->cycles_left_in_instruction += 2;
							continue;
						}
					}

					working_dividend -= shifted_divisor;
				}
			}

			/* Signed overflow detection. */
			if (!is_signed || absolute_quotient <= (result_is_negative ? 0x8000ul : 0x7FFFul))
			{
				const cc_u32f absolute_remainder = absolute_destination_value % absolute_source_value;

				const cc_u32f quotient = result_is_negative ? 0 - absolute_quotient : absolute_quotient;
				const cc_u32f remainder = destination_is_negative ? 0 - absolute_remainder : absolute_remainder;

				stuff->result_value = (quotient & 0xFFFF) | ((remainder & 0xFFFF) << 16);

				state->status_register &= ~(CONDITION_CODE_NEGATIVE | CONDITION_CODE_ZERO | CONDITION_CODE_OVERFLOW);
				state->status_register |= CONDITION_CODE_NEGATIVE & (0 - ((quotient & 0x8000) != 0));
				state->status_register |= CONDITION_CODE_ZERO & (0 - (quotient == 0));

				return;
			}
		}

		/* If we get here, then there has been overflow. */

		state->status_register |= CONDITION_CODE_OVERFLOW;
		/* These two are officially undefined, but SingleStepTests show them consistently being set to these values. */
		/* https://github.com/SingleStepTests/m68000 */
		state->status_register |= CONDITION_CODE_NEGATIVE;
		state->status_register &= ~CONDITION_CODE_ZERO;

		stuff->result_value = stuff->destination_value;
	}
}

static void Action_DIVS(Stuff* const stuff)
{
	Action_DIVCommon(stuff, cc_true);
}

static void Action_DIVU(Stuff* const stuff)
{
	Action_DIVCommon(stuff, cc_false);
}

static void Action_SUBXCommon(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	stuff->result_value = stuff->destination_value - stuff->source_value - ((state->status_register & CONDITION_CODE_EXTEND) != 0 ? 1 : 0);
}

static void Action_SUBX(Stuff* const stuff)
{
	Action_SUBXCommon(stuff);
	ADDXSUBXExecutionTime(stuff);
}

static void Action_SBCDCommon(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	/* SBCD works in two steps: a standard SUBX, followed by a standard SUB between the result and a 'correction factor'. */
	Action_SUBXCommon(stuff);

	/* The correction factor is determined by detecting both decimal and integer overflow of each
	   nibble of the result, and setting its corresponding nibble to 6 if overflow did occur. */
	/* Credit goes to Flamewing for this neat logic trick. */
	stuff->source_value = (((stuff->source_value & ~stuff->destination_value) | ((stuff->source_value | ~stuff->destination_value) & stuff->result_value)) & 0x88) << 1;
	stuff->source_value = (stuff->source_value >> 2) | (stuff->source_value >> 3);

	stuff->destination_value = stuff->result_value;

	Action_SUBCommon(stuff);

	/* Manually set the carry flag here. */
	state->status_register &= ~CONDITION_CODE_CARRY;
	state->status_register |= (stuff->source_value & 0x40) != 0 || (~stuff->destination_value & stuff->result_value & 0x80) != 0 ? CONDITION_CODE_CARRY : 0;
}

static void Action_SBCD(Stuff* const stuff)
{
	Action_SBCDCommon(stuff);
	ABCDSBCDExecutionTime(stuff);
}

static void Action_NBCD(Stuff* const stuff)
{
	stuff->source_value = stuff->destination_value;
	stuff->destination_value = 0;
	Action_SBCDCommon(stuff);

	SingleOperandInstructionExecutionTimeWordOnly(stuff, 2, 4);
}

static void Action_MULCommon(Stuff* const stuff, const cc_bool is_signed, const cc_u16f total_operations)
{
	const cc_bool multiplier_is_negative = is_signed && (stuff->source_value & 0x8000) != 0;
	const cc_bool multiplicand_is_negative = is_signed && (stuff->destination_value & 0x8000) != 0;
	const cc_bool result_is_negative = multiplier_is_negative != multiplicand_is_negative;

	const cc_u32f multiplier = multiplier_is_negative ? 0 - CC_SIGN_EXTEND_ULONG(15, stuff->source_value) : stuff->source_value;
	const cc_u32f multiplicand = multiplicand_is_negative ? 0 - CC_SIGN_EXTEND_ULONG(15, stuff->destination_value) : stuff->destination_value & 0xFFFF;

	const cc_u32f absolute_result = multiplicand * multiplier;

	stuff->result_value = result_is_negative ? 0 - absolute_result : absolute_result;

	stuff->cycles_left_in_instruction += 34 + total_operations * 2;
}

static void Action_MULS(Stuff* const stuff)
{
	const cc_u32f shifted_source_value = stuff->source_value << 1;
	const cc_u16f total_10_patterns = CountBitsSet((shifted_source_value ^ (shifted_source_value << 1)) & (0xAAAA << 1));
	const cc_u16f total_01_patterns = CountBitsSet((shifted_source_value ^ (shifted_source_value >> 1)) & (0xAAAA >> 1));

	Action_MULCommon(stuff, cc_true, total_10_patterns + total_01_patterns);
}

static void Action_MULU(Stuff* const stuff)
{
	Action_MULCommon(stuff, cc_false, CountBitsSet(stuff->source_value));
}

static void Action_ADDX(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	stuff->result_value = stuff->destination_value + stuff->source_value + ((state->status_register & CONDITION_CODE_EXTEND) != 0 ? 1 : 0);

	ADDXSUBXExecutionTime(stuff);
}

static void Action_ABCD(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	/* ABCD works in two steps: a standard ADDX, followed by a standard ADD between the result and a 'correction factor'. */
	Action_ADDX(stuff);

	/* The correction factor is determined by detecting both decimal and integer overflow of each
	   nibble of the result, and setting its corresponding nibble to 6 if overflow did occur. */
	/* Credit goes to Flamewing for this neat logic trick. */
	stuff->source_value = (((stuff->source_value & stuff->destination_value) | ((stuff->source_value | stuff->destination_value) & ~stuff->result_value)) & 0x88) << 1;
	stuff->source_value |= ((stuff->result_value + 0x66) ^ stuff->result_value) & 0x110;
	stuff->source_value = (stuff->source_value >> 2) | (stuff->source_value >> 3);

	stuff->destination_value = stuff->result_value;

	Action_ADD(stuff);

	ABCDSBCDExecutionTime(stuff);

	/* Manually set the carry flag here. */
	state->status_register &= ~CONDITION_CODE_CARRY;
	state->status_register |= (stuff->source_value & 0x40) != 0 || (stuff->destination_value & ~stuff->result_value & 0x80) != 0 ? CONDITION_CODE_CARRY : 0;
}

static void Action_EXG(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	cc_u32f temp;

	switch (stuff->opcode.raw & 0x00F8)
	{
		/* TODO: What should happen when an invalid bit pattern occurs? */
		case 0x0040:
			temp = state->data_registers[stuff->opcode.secondary_register];
			state->data_registers[stuff->opcode.secondary_register] = state->data_registers[stuff->opcode.primary_register];
			state->data_registers[stuff->opcode.primary_register] = temp;
			break;

		case 0x0048:
			temp = state->address_registers[stuff->opcode.secondary_register];
			state->address_registers[stuff->opcode.secondary_register] = state->address_registers[stuff->opcode.primary_register];
			state->address_registers[stuff->opcode.primary_register] = temp;
			break;

		case 0x0088:
			temp = state->data_registers[stuff->opcode.secondary_register];
			state->data_registers[stuff->opcode.secondary_register] = state->address_registers[stuff->opcode.primary_register];
			state->address_registers[stuff->opcode.primary_register] = temp;
			break;
	}

	stuff->cycles_left_in_instruction += 2;
}

#define DO_INSTRUCTION_ACTION_SHIFT_1_ASD \
	const unsigned long original_sign_bit = stuff->destination_value & sign_bit_bitmask;

#define DO_INSTRUCTION_ACTION_SHIFT_1_NOT_ASD

#define DO_INSTRUCTION_ACTION_SHIFT_2_MEMORY \
	count = 1;

#define DO_INSTRUCTION_ACTION_SHIFT_2_REGISTER \
	count = (stuff->opcode.raw & 0x0020) != 0 ? state->data_registers[stuff->opcode.secondary_register] % 64 : ((stuff->opcode.secondary_register - 1u) & 7u) + 1u; /* A little math trick to turn 0 into 8 */

#define DO_INSTRUCTION_ACTION_SHIFT_3_ASD \
	stuff->result_value <<= 1; \
	state->status_register |= CONDITION_CODE_OVERFLOW & (0 - ((stuff->result_value & sign_bit_bitmask) != original_sign_bit));

#define DO_INSTRUCTION_ACTION_SHIFT_3_LSD \
	stuff->result_value <<= 1;

#define DO_INSTRUCTION_ACTION_SHIFT_3_ROXD \
	stuff->result_value <<= 1; \
	stuff->result_value |= (state->status_register & CONDITION_CODE_EXTEND) != 0;

#define DO_INSTRUCTION_ACTION_SHIFT_3_ROD \
	stuff->result_value = (stuff->result_value << 1) | ((stuff->result_value & sign_bit_bitmask) != 0);

#define DO_INSTRUCTION_ACTION_SHIFT_4_NOT_ROD \
	state->status_register &= ~CONDITION_CODE_EXTEND; \
	state->status_register |= CONDITION_CODE_EXTEND & (0 - ((state->status_register & CONDITION_CODE_CARRY) != 0));

#define DO_INSTRUCTION_ACTION_SHIFT_4_ROD

#define DO_INSTRUCTION_ACTION_SHIFT_5_ASD \
	stuff->result_value >>= 1; \
	stuff->result_value |= original_sign_bit;

#define DO_INSTRUCTION_ACTION_SHIFT_5_LSD \
	stuff->result_value >>= 1;

#define DO_INSTRUCTION_ACTION_SHIFT_5_ROXD \
	stuff->result_value >>= 1; \
	stuff->result_value |= sign_bit_bitmask & (0 - ((state->status_register & CONDITION_CODE_EXTEND) != 0));

#define DO_INSTRUCTION_ACTION_SHIFT_5_ROD \
	stuff->result_value = (stuff->result_value >> 1) | (sign_bit_bitmask & (0 - ((stuff->result_value & 1) != 0)));

#define DO_INSTRUCTION_ACTION_SHIFT_6_ROXD \
	state->status_register |= CONDITION_CODE_CARRY & (0 - ((state->status_register & CONDITION_CODE_EXTEND) != 0));

#define DO_INSTRUCTION_ACTION_SHIFT_6_NOT_ROXD

#define DO_INSTRUCTION_ACTION_SHIFT(SUB_ACTION_1, SUB_ACTION_2, SUB_ACTION_3, SUB_ACTION_4, SUB_ACTION_5, SUB_ACTION_6) \
	const cc_u32f sign_bit_bitmask = (cc_u32f)1 << stuff->msb_bit_index; \
 \
	SUB_ACTION_1 \
 \
	cc_u16f i; \
	cc_u16f count; \
 \
	stuff->result_value = stuff->destination_value; \
 \
	SUB_ACTION_2; \
 \
	state->status_register &= ~(CONDITION_CODE_OVERFLOW | CONDITION_CODE_CARRY); \
 \
	SUB_ACTION_6; \
 \
	if (stuff->opcode.bit_8) \
	{ \
		/* Left */ \
		for (i = 0; i < count; ++i) \
		{ \
			state->status_register &= ~CONDITION_CODE_CARRY; \
			state->status_register |= CONDITION_CODE_CARRY & (0 - ((stuff->result_value & sign_bit_bitmask) != 0)); \
 \
			SUB_ACTION_3; \
 \
			SUB_ACTION_4; \
		} \
	} \
	else \
	{ \
		/* Right */ \
		for (i = 0; i < count; ++i) \
		{ \
			state->status_register &= ~CONDITION_CODE_CARRY; \
			state->status_register |= CONDITION_CODE_CARRY & (0 - ((stuff->result_value & 1) != 0)); \
 \
			SUB_ACTION_5; \
 \
			SUB_ACTION_4; \
		} \
	}

static void Action_ASD_MEMORY(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	DO_INSTRUCTION_ACTION_SHIFT(DO_INSTRUCTION_ACTION_SHIFT_1_ASD, DO_INSTRUCTION_ACTION_SHIFT_2_MEMORY, DO_INSTRUCTION_ACTION_SHIFT_3_ASD, DO_INSTRUCTION_ACTION_SHIFT_4_NOT_ROD, DO_INSTRUCTION_ACTION_SHIFT_5_ASD, DO_INSTRUCTION_ACTION_SHIFT_6_NOT_ROXD)
	ShiftRotateInstructionExecutionTimeMemory(stuff);
}

static void Action_ASD_REGISTER(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	DO_INSTRUCTION_ACTION_SHIFT(DO_INSTRUCTION_ACTION_SHIFT_1_ASD, DO_INSTRUCTION_ACTION_SHIFT_2_REGISTER, DO_INSTRUCTION_ACTION_SHIFT_3_ASD, DO_INSTRUCTION_ACTION_SHIFT_4_NOT_ROD, DO_INSTRUCTION_ACTION_SHIFT_5_ASD, DO_INSTRUCTION_ACTION_SHIFT_6_NOT_ROXD)
	ShiftRotateInstructionExecutionTimeRegister(stuff, count);
}

static void Action_LSD_MEMORY(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	DO_INSTRUCTION_ACTION_SHIFT(DO_INSTRUCTION_ACTION_SHIFT_1_NOT_ASD, DO_INSTRUCTION_ACTION_SHIFT_2_MEMORY, DO_INSTRUCTION_ACTION_SHIFT_3_LSD, DO_INSTRUCTION_ACTION_SHIFT_4_NOT_ROD, DO_INSTRUCTION_ACTION_SHIFT_5_LSD, DO_INSTRUCTION_ACTION_SHIFT_6_NOT_ROXD)
	ShiftRotateInstructionExecutionTimeMemory(stuff);
}

static void Action_LSD_REGISTER(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	DO_INSTRUCTION_ACTION_SHIFT(DO_INSTRUCTION_ACTION_SHIFT_1_NOT_ASD, DO_INSTRUCTION_ACTION_SHIFT_2_REGISTER, DO_INSTRUCTION_ACTION_SHIFT_3_LSD, DO_INSTRUCTION_ACTION_SHIFT_4_NOT_ROD, DO_INSTRUCTION_ACTION_SHIFT_5_LSD, DO_INSTRUCTION_ACTION_SHIFT_6_NOT_ROXD)
	ShiftRotateInstructionExecutionTimeRegister(stuff, count);
}

static void Action_ROD_MEMORY(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	DO_INSTRUCTION_ACTION_SHIFT(DO_INSTRUCTION_ACTION_SHIFT_1_NOT_ASD, DO_INSTRUCTION_ACTION_SHIFT_2_MEMORY, DO_INSTRUCTION_ACTION_SHIFT_3_ROD, DO_INSTRUCTION_ACTION_SHIFT_4_ROD, DO_INSTRUCTION_ACTION_SHIFT_5_ROD, DO_INSTRUCTION_ACTION_SHIFT_6_NOT_ROXD)
	ShiftRotateInstructionExecutionTimeMemory(stuff);
}

static void Action_ROD_REGISTER(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	DO_INSTRUCTION_ACTION_SHIFT(DO_INSTRUCTION_ACTION_SHIFT_1_NOT_ASD, DO_INSTRUCTION_ACTION_SHIFT_2_REGISTER, DO_INSTRUCTION_ACTION_SHIFT_3_ROD, DO_INSTRUCTION_ACTION_SHIFT_4_ROD, DO_INSTRUCTION_ACTION_SHIFT_5_ROD, DO_INSTRUCTION_ACTION_SHIFT_6_NOT_ROXD)
	ShiftRotateInstructionExecutionTimeRegister(stuff, count);
}

static void Action_ROXD_MEMORY(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	DO_INSTRUCTION_ACTION_SHIFT(DO_INSTRUCTION_ACTION_SHIFT_1_NOT_ASD, DO_INSTRUCTION_ACTION_SHIFT_2_MEMORY, DO_INSTRUCTION_ACTION_SHIFT_3_ROXD, DO_INSTRUCTION_ACTION_SHIFT_4_NOT_ROD, DO_INSTRUCTION_ACTION_SHIFT_5_ROXD, DO_INSTRUCTION_ACTION_SHIFT_6_ROXD)
	ShiftRotateInstructionExecutionTimeMemory(stuff);
}

static void Action_ROXD_REGISTER(Stuff* const stuff)
{
	Clown68000_State* const state = stuff->state;

	DO_INSTRUCTION_ACTION_SHIFT(DO_INSTRUCTION_ACTION_SHIFT_1_NOT_ASD, DO_INSTRUCTION_ACTION_SHIFT_2_REGISTER, DO_INSTRUCTION_ACTION_SHIFT_3_ROXD, DO_INSTRUCTION_ACTION_SHIFT_4_NOT_ROD, DO_INSTRUCTION_ACTION_SHIFT_5_ROXD, DO_INSTRUCTION_ACTION_SHIFT_6_ROXD)
	ShiftRotateInstructionExecutionTimeRegister(stuff, count);
}

static void Action_UNIMPLEMENTED_1(Stuff* const stuff)
{
	Group1Or2Exception(stuff, 10);
}

static void Action_UNIMPLEMENTED_2(Stuff* const stuff)
{
	Group1Or2Exception(stuff, 11);
}

static void Action_NOP(Stuff* const stuff)
{
	(void)stuff;

	/* Doesn't do anything */
}


/* API */

void Clown68000_Reset(Clown68000_State *state, const Clown68000_ReadWriteCallbacks *callbacks)
{
	Stuff stuff;

	stuff.state = state;
	stuff.callbacks = callbacks;

	if (!clown68000_setjmp(stuff.exception.context))
	{
		state->halted = cc_false;
		state->stopped = cc_false;
		state->pending_interrupt = 0; /* TODO: Does a reset actually clear the pending interrupt? */
		state->leftover_cycles = 0;

		/* Disable trace mode. */
		state->status_register &= ~STATUS_TRACE;
		/* Set interrupt mask to 7 */
		state->status_register |= 0x0700;
		/* Set supervisor bit */
		SetSupervisorMode(state, cc_true);

		state->address_registers[7] = ReadLongWord(&stuff, 0);
		state->program_counter = ReadLongWord(&stuff, 4);
	}
}

void Clown68000_Interrupt(Clown68000_State *state, cc_u16f level)
{
	assert(level >= 1 && level <= 7);
	state->pending_interrupt = level;
}

/* Pre-instruction hook for hybrid dispatch. Set by the runner to intercept
 * execution at specific PCs before the interpreter fetches the opcode. */
extern void (*g_hybrid_pre_insn_fn)(cc_u32l pc);

void Clown68000_DoCycles(Clown68000_State *state, const Clown68000_ReadWriteCallbacks *callbacks, const cc_u32f cycles_to_do)
{
	/* Initialise closure and exception stuff. */
	Stuff stuff;

	stuff.state = state;
	stuff.callbacks = callbacks;
	stuff.cycles_left_in_instruction = state->leftover_cycles;
	stuff.cycles_done = 0;

	if (!state->halted)
	{
		/* Normally, this would go in the instruction loop, but we want to call this as rarely
		   as possible (for efficiency's sake), so we call it outside the loop instead. */
		switch (clown68000_setjmp(stuff.exception.context))
		{
			case 1:
				/* Group 0 exception. */
				/* Handled elsewhere. */
				break;

			case 2:
				/* Group 1/2 exception. */
				state->program_counter = stuff.starting_program_counter;
				DoInterrupt(&stuff, stuff.exception.vector_offset);
				break;
		}

		while ((stuff.cycles_done += stuff.cycles_left_in_instruction) <= cycles_to_do)
		{
			stuff.cycles_left_in_instruction = 4;
			stuff.starting_program_counter = state->program_counter;

			/* Trace Sonic_Move execution after jump */
			{
				static int s_jump_pc_trace = 0;
				static int s_jump_pc_count = 0;
				/* Detect: PC enters Sonic_Jump ($0133EA) */
				if (state->program_counter == 0x0133EA && !s_jump_pc_trace) {
					s_jump_pc_trace = 1;
					s_jump_pc_count = 0;
				}
				/* Trace PCs in Sonic_Move range */
				if (s_jump_pc_trace && s_jump_pc_count < 200) {
					cc_u32f pc = state->program_counter;
					if (pc >= 0x012EB0 && pc <= 0x013010) {
						fprintf(stderr, "[PC-TRACE] $%06X\n", (unsigned)pc);
					}
					/* Stop after Sonic_Move's yvel write or after too many instructions */
					if (pc == 0x01300C) { /* PC after yvel write */
						fprintf(stderr, "[PC-TRACE] *** reached $013008 yvel write ***\n");
						s_jump_pc_trace = 0;
					}
					s_jump_pc_count++;
					if (s_jump_pc_count >= 200) {
						fprintf(stderr, "[PC-TRACE] *** 200 insns without reaching $01300C — Sonic_Move SKIPPED ***\n");
						s_jump_pc_trace = 0;
					}
				}
			}

			if (g_hybrid_pre_insn_fn != NULL)
			{
				extern cc_u32f g_hybrid_cycle_counter;
				g_hybrid_cycle_counter = stuff.cycles_done;
				g_hybrid_pre_insn_fn(state->program_counter);
			}

			if (!state->stopped)
			{
				/* Process next instruction. */

				/* Figure out which instruction this is. */
				const Instruction instruction = DecodeOpcode(&stuff.opcode, ReadWord(&stuff, state->program_counter));

				/* We already pre-fetched the instruction, so just advance past it. */
				state->instruction_register = stuff.opcode.raw;
				IncrementProgramCounter(state, 2);

				switch (instruction)
				{
					#include "microcode.c"
				}
			}

			/* TODO: Does this occur before or after instruction processing? Apparently a Sesame Street game depends on a one-instruction latency.
				https://gendev.spritesmind.net/forum/viewtopic.php?t=2202 */
			/* Process pending interrupt. */
			if (state->pending_interrupt == 7 || state->pending_interrupt > (((cc_u16f)state->status_register >> 8) & 7))
			{
				state->stopped = cc_false;

				DoInterrupt(&stuff, 24 + state->pending_interrupt);

				/* TODO: Integrate this into the exception logic, and give all exceptions proper durations. */
				/* TODO: Didn't James Groth mention that this should be 24 in one of his blog posts? */
				stuff.cycles_left_in_instruction += 14;

				/* Set interrupt mask to current level */
				state->status_register &= ~STATUS_INTERRUPT_MASK;
				state->status_register |= state->pending_interrupt << 8;

				state->pending_interrupt = 0;
			}
		}

		state->leftover_cycles = stuff.cycles_done - cycles_to_do;
	}
}
