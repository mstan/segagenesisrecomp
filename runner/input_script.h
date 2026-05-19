/*
 * input_script.h — frame-timeline input scripting for the Genesis
 * runner. Modeled after nesrecomp's input_script: a plain-text
 * timeline of button hold/release events, RAM assertions, waits,
 * and exit triggers, executed once per wall frame.
 *
 * Format (one directive per line, # comments, blank lines OK):
 *
 *   WAIT N               wait N frames before processing the next line
 *   HOLD <BUTTON>        start holding the button (until RELEASE or EXIT)
 *   RELEASE [BUTTON]     release one button; with no arg, release all
 *   PRESS <BUTTON> N     hold for N frames then auto-release
 *   ASSERT_RAM8 <addr> <value>    fail-fast if RAM byte != value
 *   WAIT_RAM8   <addr> <value>    block until RAM byte == value
 *   ASSERT_RAM16 <addr> <value>   ditto, 16-bit big-endian
 *   WAIT_RAM16   <addr> <value>
 *   WRITE_RAM8   <addr> <value>   write byte to 68K address
 *   WRITE_RAM16  <addr> <value>   write big-endian word to 68K address
 *   WRITE_RAM32  <addr> <value>   write big-endian long to 68K address
 *   SCREENSHOT  <path>   trigger a PNG capture (path templated with frame)
 *   SAVE_STATE  <path>   save a native/interpreter state file
 *   LOAD_STATE  <path>   load a native/interpreter state file
 *   DUMP_RAM    <path>   dump canonical 68K byte-ordered work RAM
 *   EXIT [code]          exit cleanly with optional status code
 *
 * BUTTON names: UP DOWN LEFT RIGHT A B C START
 *
 * Loaded once at startup via input_script_load(). Each wall frame
 * the runner calls input_script_tick(frame). Held-button state is
 * exposed as an 8-bit mask matching the Genesis layout
 * (Up=bit0, Down=1, Left=2, Right=3, B=4, C=5, A=6, Start=7) for
 * the input callback to OR into the joypad read.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Load and parse a script file. Returns 1 on success, 0 on file
 * not found or parse error. Calling with NULL disables scripting. */
int  input_script_load(const char *path);

/* Advance the script by one wall frame. `ram_read8`/`ram_read16` are
 * supplied by the runner so the script can implement
 * ASSERT_RAM* / WAIT_RAM* without being coupled to glue.c. Returns
 * an exit code if the script asked to exit (use input_script_should_exit
 * to test, then input_script_exit_code to retrieve). */
typedef uint8_t  (*input_script_read8_fn) (uint32_t addr);
typedef uint16_t (*input_script_read16_fn)(uint32_t addr);
typedef void     (*input_script_write8_fn) (uint32_t addr, uint8_t  value);
typedef void     (*input_script_write16_fn)(uint32_t addr, uint16_t value);
typedef void     (*input_script_write32_fn)(uint32_t addr, uint32_t value);
void input_script_tick(uint64_t frame,
                       input_script_read8_fn  r8,
                       input_script_read16_fn r16,
                       input_script_write8_fn  w8,
                       input_script_write16_fn w16,
                       input_script_write32_fn w32);

/* Currently-held buttons as an 8-bit mask:
 *   bit 0 UP    bit 4 B
 *   bit 1 DOWN  bit 5 C
 *   bit 2 LEFT  bit 6 A
 *   bit 3 RIGHT bit 7 START
 */
uint8_t input_script_held_mask(void);

/* True iff the script ran an EXIT directive. The caller should
 * cleanly tear down (close TCP server, flush logs, exit). */
bool    input_script_should_exit(void);
int     input_script_exit_code  (void);

/* Returns true and copies the pending path when a script requested a
 * save-state operation this frame. The main loop owns actual I/O because
 * it has access to the emulator instance. */
bool    input_script_take_save_state(char *out, size_t out_cap);
bool    input_script_take_screenshot(char *out, size_t out_cap);
bool    input_script_take_load_state(char *out, size_t out_cap);
bool    input_script_take_ram_dump(char *out, size_t out_cap);

/* True iff a script is currently loaded and active. Lets callers
 * branch (e.g. ignore SDL input when scripted). */
bool    input_script_active(void);
