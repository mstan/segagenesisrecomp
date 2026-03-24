#ifndef CLOWNZ80_INTERPRETER_H
#define CLOWNZ80_INTERPRETER_H

/* If enabled, a lookup table is used to optimise opcode decoding. Disable this to save RAM. */
#define CLOWNZ80_PRECOMPUTE_INSTRUCTION_METADATA

#include "../libraries/clowncommon/clowncommon.h"

typedef struct ClownZ80_State
{
	cc_u8l register_mode; /* ClownZ80_RegisterMode */
	cc_u16l cycles;
	cc_u16l program_counter;
	cc_u16l stack_pointer;
	cc_u8l a, f, b, c, d, e, h, l;
	cc_u8l a_, f_, b_, c_, d_, e_, h_, l_; /* Backup registers. */
	cc_u8l ixh, ixl, iyh, iyl;
	cc_u8l r, i;
	cc_bool interrupts_enabled;
	cc_bool interrupt_pending;
} ClownZ80_State;

typedef struct ClownZ80_ReadAndWriteCallbacks
{
	cc_u16f (*read)(void *user_data, cc_u16f address);
	void (*write)(void *user_data, cc_u16f address, cc_u16f value);
	CC_ATTRIBUTE_PRINTF(2, 3) void (*log)(void *user_data, const char* const format, ...);
	const void *user_data;
} ClownZ80_ReadAndWriteCallbacks;

void ClownZ80_Constant_Initialise(void);
void ClownZ80_State_Initialise(ClownZ80_State *state);
void ClownZ80_Reset(ClownZ80_State *state);
void ClownZ80_Interrupt(ClownZ80_State *state, cc_bool assert_interrupt);
cc_u16f ClownZ80_DoInstruction(ClownZ80_State *state, const ClownZ80_ReadAndWriteCallbacks *callbacks);

#endif /* CLOWNZ80_INTERPRETER_H */
