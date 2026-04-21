/*
 * cycle_probe.c — runs clown68000 on a stub bus at codegen time to measure
 * the exact per-instruction cycle cost.
 *
 * Mechanism (clown68000.c:2461-2515):
 *   state->leftover_cycles = 0;
 *   Clown68000_DoCycles(state, &cb, 0);
 *   // Loop condition `(cycles_done += leftover) <= 0` is TRUE at entry
 *   // (both zero), enters body, executes one instruction, then the
 *   // condition flips FALSE and the loop exits. leftover_cycles is
 *   // written back as `cycles_done - 0` = the instruction's full cost.
 *
 * One call = one instruction, regardless of the instruction's actual length
 * in cycles. Verified against the setjmp handler at clown68000.c:2447: on
 * any group-0/1/2 exception DoCycles still returns cleanly, so an illegal
 * opcode or a bus-error trap never crashes codegen.
 */

#include "cycle_probe.h"
#include "clown68000.h"
#include <stdint.h>
#include <string.h>

/* --- Linker-visible symbols the submodule patch declares as extern.
 *     clown68000.c references these from inside DoCycles. Leave the
 *     pre-insn hook NULL so clown's hybrid-dispatch path is a no-op
 *     during codegen; provide the cycle counter as a plain storage
 *     location (its value is irrelevant here). --- */
void (*g_hybrid_pre_insn_fn)(cc_u32l pc) = 0;
cc_u32f g_hybrid_cycle_counter = 0;

/* --- Stub bus --- */

typedef struct {
    const uint8_t *rom;
    uint32_t       rom_size;
} StubBus;

static StubBus                         s_bus;
static int                             s_initialized = 0;
static Clown68000_ReadWriteCallbacks   s_cb;

/* clown68000's callbacks use WORD-granularity addressing: the address arg is
 * `byte_address / 2`. We reverse that to index the ROM byte array. */
static cc_u16f stub_read(const void *user, cc_u32f word_addr,
                         cc_bool do_hi, cc_bool do_lo, cc_u32f cycle)
{
    (void)do_hi; (void)do_lo; (void)cycle;
    const StubBus *b = (const StubBus *)user;
    uint32_t byte_addr = (uint32_t)(word_addr * 2);
    if (byte_addr + 1 < b->rom_size) {
        return ((uint16_t)b->rom[byte_addr] << 8) | b->rom[byte_addr + 1];
    }
    return 0;   /* Non-ROM regions: return zero. */
}

static void stub_write(const void *user, cc_u32f word_addr,
                       cc_bool do_hi, cc_bool do_lo, cc_u32f cycle, cc_u16f val)
{
    (void)user; (void)word_addr; (void)do_hi; (void)do_lo; (void)cycle; (void)val;
    /* Drop all writes. */
}

int cycle_probe_init(const GenesisRom *rom)
{
    if (!rom || !rom->rom_data || rom->rom_size == 0) return -1;
    s_bus.rom      = rom->rom_data;
    s_bus.rom_size = rom->rom_size;
    s_cb.read_callback  = stub_read;
    s_cb.write_callback = stub_write;
    s_cb.user_data      = &s_bus;
    s_initialized = 1;
    return 0;
}

void cycle_probe_shutdown(void)
{
    s_initialized = 0;
}

int cycle_probe_measure(uint32_t addr)
{
    if (!s_initialized) return -1;

    /* Fresh state per measurement. Mid-range synthetic operands so
     * data-dependent costs (MULx/DIVx popcount, register-count shifts)
     * land near the average rather than at an extreme. */
    Clown68000_State st;
    memset(&st, 0, sizeof(st));
    st.program_counter          = addr;
    st.address_registers[7]     = 0x00FFFE00u;   /* Sonic 1 initial SSP */
    st.supervisor_stack_pointer = 0x00FFFE00u;
    st.user_stack_pointer       = 0x00FFFE00u;
    st.status_register          = 0x2700;        /* supervisor, IRQ-masked */
    for (int i = 0; i < 7; i++) st.data_registers[i]    = 0x5555u;
    st.data_registers[7]        = 4;             /* avg shift count */
    for (int i = 0; i < 7; i++) st.address_registers[i] = 0x00FFFE00u;
    st.leftover_cycles          = 0;

    Clown68000_DoCycles(&st, &s_cb, 0);
    return (int)st.leftover_cycles;
}
