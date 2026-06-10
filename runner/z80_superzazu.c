/*
 * z80_superzazu.c — AGPL-free Z80 core for the runner.
 *
 * Provides the clownz80 public API (ClownZ80_Constant_Initialise /
 * State_Initialise / Reset / Interrupt / DoInstruction) backed by superzazu/z80
 * (Nicolas Allemand, MIT; vendored at runner/external/superzazu/). Compiled into
 * the final executable, these definitions WIN over clownz80-interpreter.lib via
 * MSVC's first-wins linker rule — the same trick stub_clown68000.c uses for the
 * 68000 — so clownz80's AGPL interpreter object is never pulled into the binary.
 * clownz80 itself stays pristine upstream (no fork, no edits).
 *
 * This file is throwaway glue between our MIT Z80 core and clownmdemu's
 * (AGPL) emulator: it references clownmdemu/clownz80's ClownZ80_State layout
 * (interpreter.h) so clownmdemu can keep calling/snapshotting the Z80 unchanged.
 * It dies when clownmdemu-core is eventually removed. See
 * segagenesisrecomp/LICENSING.md (teardown item #3).
 *
 * Design: ClownZ80_State stays clownmdemu's canonical, plain-data register store
 * (so its struct-copy save/restore `backup->z80 = clownmdemu->z80` and all
 * snapshot/observability readers keep working). superzazu's z80 is a transient
 * execution engine driven from it each instruction.
 *
 * The five Z80 bits clownz80's struct does NOT carry (IM, IFF2, halted, EI
 * delay, WZ) live in a file-static here — there is exactly one Z80. The only
 * consequence: clownmdemu's save-state backup/restore (used by the DEBUG rewind
 * ring, never by normal play) does not capture these five hidden flags. Harmless
 * for release. If perfect debug-rewind fidelity is ever wanted, add the fields
 * to ClownZ80_State (a clownz80 edit) instead.
 *
 * Interrupt fidelity: superzazu's z80_step executes the instruction and THEN
 * runs process_interrupts — identical ordering to clownz80 (IM1 interrupt taken
 * after the instruction, suppressed after EI via iff_delay and after DD/FD
 * because a prefixed op is one step). Sonic's SMPS driver uses IM 1.
 */
#include "interpreter.h"   /* clownz80 (pristine): ClownZ80_State + the API */
#include "z80.h"           /* superzazu */

#include <stddef.h>

/* Hidden Z80 state clownz80's ClownZ80_State doesn't carry. One Z80 → static. */
static struct {
	cc_u8l  interrupt_mode;
	cc_bool iff2;
	cc_bool halted;
	cc_u8l  iff_delay;
	cc_u16l mem_ptr;
} s_ext;

/* ---- Z80 F-register <-> superzazu flag bitfields -------------------------- */
/* F layout: S Z Y H X P/V N C  (bit7 .. bit0). */
static void unpack_flags(z80 *z, cc_u8l f)
{
	z->sf = (f >> 7) & 1; z->zf = (f >> 6) & 1; z->yf = (f >> 5) & 1; z->hf = (f >> 4) & 1;
	z->xf = (f >> 3) & 1; z->pf = (f >> 2) & 1; z->nf = (f >> 1) & 1; z->cf = (f >> 0) & 1;
}

static cc_u8l pack_flags(const z80 *z)
{
	return (cc_u8l)(
		(z->sf << 7) | (z->zf << 6) | (z->yf << 5) | (z->hf << 4) |
		(z->xf << 3) | (z->pf << 2) | (z->nf << 1) | (z->cf << 0));
}

/* ---- Memory / IO thunks: forward to the clownmdemu bus callbacks ----------- */
/* z->userdata holds the ClownZ80_ReadAndWriteCallbacks* for this call. The
 * Genesis Z80 has no separate I/O space wired up; clownz80 collapses memory and
 * IO onto its single read/write callback, so port IN/OUT route there too (using
 * the 16-bit BC port address). Sonic's driver never executes IN/OUT. */
static uint8_t sz_read(void *ud, uint16_t address)
{
	const ClownZ80_ReadAndWriteCallbacks *cb = (const ClownZ80_ReadAndWriteCallbacks*)ud;
	return (uint8_t)cb->read((void*)cb->user_data, address);
}

static void sz_write(void *ud, uint16_t address, uint8_t value)
{
	const ClownZ80_ReadAndWriteCallbacks *cb = (const ClownZ80_ReadAndWriteCallbacks*)ud;
	cb->write((void*)cb->user_data, address, value);
}

static uint8_t sz_port_in(z80 *z, uint8_t port)
{
	const ClownZ80_ReadAndWriteCallbacks *cb = (const ClownZ80_ReadAndWriteCallbacks*)z->userdata;
	return (uint8_t)cb->read((void*)cb->user_data, (cc_u16f)((z->b << 8) | port));
}

static void sz_port_out(z80 *z, uint8_t port, uint8_t value)
{
	const ClownZ80_ReadAndWriteCallbacks *cb = (const ClownZ80_ReadAndWriteCallbacks*)z->userdata;
	cb->write((void*)cb->user_data, (cc_u16f)((z->b << 8) | port), value);
}

/* ---- ClownZ80_State (+ s_ext) <-> superzazu z80 --------------------------- */
static void load_state(z80 *z, const ClownZ80_State *s,
                       const ClownZ80_ReadAndWriteCallbacks *callbacks)
{
	z->read_byte = sz_read; z->write_byte = sz_write;
	z->port_in = sz_port_in; z->port_out = sz_port_out;
	z->userdata = (void*)callbacks;
	z->cyc = 0;

	z->pc = s->program_counter;
	z->sp = s->stack_pointer;
	z->ix = (uint16_t)((s->ixh << 8) | s->ixl);
	z->iy = (uint16_t)((s->iyh << 8) | s->iyl);
	z->mem_ptr = s_ext.mem_ptr;

	z->a = s->a; z->b = s->b; z->c = s->c; z->d = s->d;
	z->e = s->e; z->h = s->h; z->l = s->l;
	unpack_flags(z, s->f);

	z->a_ = s->a_; z->b_ = s->b_; z->c_ = s->c_; z->d_ = s->d_;
	z->e_ = s->e_; z->h_ = s->h_; z->l_ = s->l_; z->f_ = s->f_;

	z->i = s->i;
	z->r = s->r;

	z->interrupt_mode = s_ext.interrupt_mode;
	z->iff1   = s->interrupts_enabled ? 1 : 0;
	z->iff2   = s_ext.iff2 ? 1 : 0;
	z->halted = s_ext.halted ? 1 : 0;
	z->iff_delay = s_ext.iff_delay;

	z->int_pending = s->interrupt_pending ? 1 : 0;
	z->int_data    = 0xFF; /* IM1 ignores the data bus. */
	z->nmi_pending = 0;    /* Genesis Z80 has no NMI source. */
}

static void store_state(ClownZ80_State *s, const z80 *z)
{
	s->program_counter = z->pc;
	s->stack_pointer   = z->sp;
	s->ixh = (cc_u8l)(z->ix >> 8); s->ixl = (cc_u8l)(z->ix & 0xFF);
	s->iyh = (cc_u8l)(z->iy >> 8); s->iyl = (cc_u8l)(z->iy & 0xFF);
	s_ext.mem_ptr = z->mem_ptr;

	s->a = z->a; s->b = z->b; s->c = z->c; s->d = z->d;
	s->e = z->e; s->h = z->h; s->l = z->l;
	s->f = pack_flags(z);

	s->a_ = z->a_; s->b_ = z->b_; s->c_ = z->c_; s->d_ = z->d_;
	s->e_ = z->e_; s->h_ = z->h_; s->l_ = z->l_; s->f_ = z->f_;

	s->i = z->i;
	s->r = z->r;

	s_ext.interrupt_mode  = z->interrupt_mode;
	s->interrupts_enabled = z->iff1 ? cc_true : cc_false;
	s_ext.iff2   = z->iff2 ? cc_true : cc_false;
	s_ext.halted = z->halted ? cc_true : cc_false;
	s_ext.iff_delay = (cc_u8l)z->iff_delay;

	s->interrupt_pending = z->int_pending ? cc_true : cc_false;
}

/* ---- Public API (overrides clownz80-interpreter.lib via first-wins) -------- */
void ClownZ80_Constant_Initialise(void)
{
	/* superzazu needs no precomputed tables. */
}

void ClownZ80_State_Initialise(ClownZ80_State *state)
{
	z80 z;
	z80_init(&z);          /* superzazu's documented post-power-on defaults. */
	store_state(state, &z);
	state->register_mode = 0;
	ClownZ80_Reset(state);
	state->cycles = 1;
}

void ClownZ80_Reset(ClownZ80_State *state)
{
	/* Match clownz80's reset (PC=0, interrupts disabled); leave general-purpose
	 * registers, which is also what clownz80 does. */
	state->register_mode      = 0;
	state->program_counter    = 0;
	state->interrupts_enabled = cc_false;
	state->interrupt_pending  = cc_false;
	s_ext.interrupt_mode = 0;
	s_ext.iff2 = cc_false;
	s_ext.halted = cc_false;
	s_ext.iff_delay = 0;
}

void ClownZ80_Interrupt(ClownZ80_State *state, const cc_bool assert_interrupt)
{
	state->interrupt_pending = assert_interrupt;
}

cc_u16f ClownZ80_DoInstruction(ClownZ80_State *state,
                               const ClownZ80_ReadAndWriteCallbacks *callbacks)
{
	z80 z;
	load_state(&z, state, callbacks);
	z80_step(&z);                 /* one instruction, then process_interrupts */
	store_state(state, &z);
	state->cycles = (cc_u16l)z.cyc;
	return (cc_u16f)z.cyc;
}

/* Save-state hooks for the five hidden Z80 bits clownz80's struct doesn't
 * carry (IM, IFF2, halted, EI delay, WZ). The scheduler's ClownZ80_State is
 * snapshotted by machine_save_state; these complete the Z80's state. */
#include <stdio.h>
int z80sz_save_ext(FILE *f)
{
	return fwrite(&s_ext, sizeof s_ext, 1, f) == 1;
}
int z80sz_load_ext(FILE *f)
{
	return fread(&s_ext, sizeof s_ext, 1, f) == 1;
}
