/*
 * input_script.c — see input_script.h for the format spec.
 *
 * Implementation is a step-machine over a pre-parsed instruction
 * array. Each tick processes instructions until one blocks (WAIT,
 * WAIT_RAM*) or the array runs out. ASSERTs that fail call the
 * crash-report dump and exit. Held-button mask survives tick
 * boundaries until explicitly RELEASEd.
 */

#include "input_script.h"
#include "crash_report.h"
#include "genesis_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ---- Op codes ---- */

typedef enum {
    OP_WAIT,
    OP_HOLD,
    OP_RELEASE,        /* arg=0xFF means RELEASE_ALL */
    OP_PRESS,          /* arg = button, count = frames to hold */
    OP_ASSERT_RAM8,
    OP_WAIT_RAM8,
    OP_ASSERT_RAM16,
    OP_WAIT_RAM16,
    OP_WRITE_RAM8,
    OP_WRITE_RAM16,
    OP_WRITE_RAM32,
    OP_SCREENSHOT,
    OP_SAVE_STATE,
    OP_LOAD_STATE,
    OP_DUMP_RAM,
    OP_EXIT,
} OpCode;

typedef struct {
    OpCode    op;
    uint32_t  arg32;       /* WAIT count, RAM addr, EXIT code */
    uint32_t  arg32b;      /* RAM expected value, PRESS frames */
    uint8_t   button_mask; /* HOLD/RELEASE/PRESS button bit */
    int       line_no;     /* source line for error messages */
    char      str[260];    /* path/string argument */
} ScriptOp;

/* ---- State ---- */

#define MAX_OPS 4096
static ScriptOp s_ops[MAX_OPS];
static int      s_op_count = 0;
static int      s_pc       = 0;     /* next op to execute */
static int      s_active   = 0;
static uint64_t s_wait_until_frame = 0;
static uint8_t  s_held_mask        = 0;
static int      s_exit_pending     = 0;
static int      s_exit_code        = 0;
static char     s_pending_screenshot[260];
static char     s_pending_save_state[260];
static char     s_pending_load_state[260];
static char     s_pending_ram_dump[260];

/* PRESS auto-release tracking. Up to 4 simultaneous PRESS timers. */
typedef struct { uint8_t mask; uint64_t release_frame; } PressTimer;
static PressTimer s_press_timers[4];

/* ---- Button name parsing ---- */

static int parse_button(const char *s, uint8_t *out) {
    if      (!_stricmp(s, "UP"))    *out = 1u << 0;
    else if (!_stricmp(s, "DOWN"))  *out = 1u << 1;
    else if (!_stricmp(s, "LEFT"))  *out = 1u << 2;
    else if (!_stricmp(s, "RIGHT")) *out = 1u << 3;
    else if (!_stricmp(s, "B"))     *out = 1u << 4;
    else if (!_stricmp(s, "C"))     *out = 1u << 5;
    else if (!_stricmp(s, "A"))     *out = 1u << 6;
    else if (!_stricmp(s, "START")) *out = 1u << 7;
    else return 0;
    return 1;
}

/* ---- Parser ---- */

static int parse_line(char *line, int line_no) {
    /* Strip comment + trailing whitespace */
    char *hash = strchr(line, '#');
    if (hash) *hash = '\0';
    char *end = line + strlen(line);
    while (end > line && isspace((unsigned char)end[-1])) *--end = '\0';
    char *p = line;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == '\0') return 1;     /* blank line OK */

    if (s_op_count >= MAX_OPS) {
        fprintf(stderr, "input_script: too many directives (>%d)\n", MAX_OPS);
        return 0;
    }
    ScriptOp *o = &s_ops[s_op_count];
    o->line_no = line_no;
    o->str[0]  = '\0';

    char tok[64], a1[260], a2[260];
    int n = sscanf(p, "%63s %259s %259s", tok, a1, a2);
    if (n < 1) return 1;

    if (!_stricmp(tok, "WAIT") && n >= 2) {
        o->op    = OP_WAIT;
        o->arg32 = (uint32_t)strtoul(a1, NULL, 0);
    } else if (!_stricmp(tok, "HOLD") && n >= 2) {
        if (!parse_button(a1, &o->button_mask)) goto bad;
        o->op = OP_HOLD;
    } else if (!_stricmp(tok, "RELEASE")) {
        o->op = OP_RELEASE;
        if (n >= 2) {
            if (!parse_button(a1, &o->button_mask)) goto bad;
        } else {
            o->button_mask = 0xFF;  /* sentinel: release all */
        }
    } else if (!_stricmp(tok, "PRESS") && n >= 3) {
        if (!parse_button(a1, &o->button_mask)) goto bad;
        o->op     = OP_PRESS;
        o->arg32b = (uint32_t)strtoul(a2, NULL, 0);
    } else if ((!_stricmp(tok, "ASSERT_RAM8") || !_stricmp(tok, "WAIT_RAM8")) && n >= 3) {
        o->op     = !_stricmp(tok, "ASSERT_RAM8") ? OP_ASSERT_RAM8 : OP_WAIT_RAM8;
        o->arg32  = (uint32_t)strtoul(a1, NULL, 16);
        o->arg32b = (uint32_t)strtoul(a2, NULL, 16);
    } else if ((!_stricmp(tok, "ASSERT_RAM16") || !_stricmp(tok, "WAIT_RAM16")) && n >= 3) {
        o->op     = !_stricmp(tok, "ASSERT_RAM16") ? OP_ASSERT_RAM16 : OP_WAIT_RAM16;
        o->arg32  = (uint32_t)strtoul(a1, NULL, 16);
        o->arg32b = (uint32_t)strtoul(a2, NULL, 16);
    } else if (!_stricmp(tok, "WRITE_RAM8") && n >= 3) {
        o->op     = OP_WRITE_RAM8;
        o->arg32  = (uint32_t)strtoul(a1, NULL, 16);
        o->arg32b = (uint32_t)strtoul(a2, NULL, 16);
    } else if (!_stricmp(tok, "WRITE_RAM16") && n >= 3) {
        o->op     = OP_WRITE_RAM16;
        o->arg32  = (uint32_t)strtoul(a1, NULL, 16);
        o->arg32b = (uint32_t)strtoul(a2, NULL, 16);
    } else if (!_stricmp(tok, "WRITE_RAM32") && n >= 3) {
        o->op     = OP_WRITE_RAM32;
        o->arg32  = (uint32_t)strtoul(a1, NULL, 16);
        o->arg32b = (uint32_t)strtoul(a2, NULL, 16);
    } else if (!_stricmp(tok, "SCREENSHOT")) {
        o->op = OP_SCREENSHOT;
        if (n >= 2) snprintf(o->str, sizeof(o->str), "%s", a1);
        else        snprintf(o->str, sizeof(o->str), "screenshot.png");
    } else if (!_stricmp(tok, "SAVE_STATE") && n >= 2) {
        o->op = OP_SAVE_STATE;
        snprintf(o->str, sizeof(o->str), "%s", a1);
    } else if (!_stricmp(tok, "LOAD_STATE") && n >= 2) {
        o->op = OP_LOAD_STATE;
        snprintf(o->str, sizeof(o->str), "%s", a1);
    } else if (!_stricmp(tok, "DUMP_RAM") && n >= 2) {
        o->op = OP_DUMP_RAM;
        snprintf(o->str, sizeof(o->str), "%s", a1);
    } else if (!_stricmp(tok, "EXIT")) {
        o->op    = OP_EXIT;
        o->arg32 = (n >= 2) ? (uint32_t)strtoul(a1, NULL, 0) : 0u;
    } else {
        goto bad;
    }
    s_op_count++;
    return 1;

bad:
    fprintf(stderr, "input_script: parse error at line %d: %s\n", line_no, p);
    return 0;
}

int input_script_load(const char *path) {
    s_op_count = 0;
    s_pc = 0;
    s_active = 0;
    s_wait_until_frame = 0;
    s_held_mask = 0;
    s_exit_pending = 0;
    s_pending_screenshot[0] = '\0';
    s_pending_save_state[0] = '\0';
    s_pending_load_state[0] = '\0';
    s_pending_ram_dump[0] = '\0';
    memset(s_press_timers, 0, sizeof(s_press_timers));
    if (!path) return 0;

    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "input_script: cannot open '%s'\n", path);
        return 0;
    }
    char line[256];
    int line_no = 0;
    while (fgets(line, sizeof(line), f)) {
        line_no++;
        if (!parse_line(line, line_no)) { fclose(f); return 0; }
    }
    fclose(f);
    s_active = 1;
    fprintf(stderr, "input_script: loaded %d directives from %s\n", s_op_count, path);
    return 1;
}

void input_script_tick(uint64_t frame,
                       input_script_read8_fn  r8,
                       input_script_read16_fn r16,
                       input_script_write8_fn  w8,
                       input_script_write16_fn w16,
                       input_script_write32_fn w32)
{
    if (!s_active) return;

    /* Auto-release any PRESS that has timed out. */
    for (int i = 0; i < 4; i++) {
        if (s_press_timers[i].mask && frame >= s_press_timers[i].release_frame) {
            s_held_mask &= (uint8_t)~s_press_timers[i].mask;
            s_press_timers[i].mask = 0;
        }
    }

    /* Honor outstanding WAIT. */
    if (frame < s_wait_until_frame) return;

    /* Drain instructions until we block. */
    while (s_pc < s_op_count) {
        ScriptOp *o = &s_ops[s_pc];
        switch (o->op) {
            case OP_WAIT:
                s_wait_until_frame = frame + o->arg32;
                s_pc++;
                if (frame < s_wait_until_frame) return;
                break;

            case OP_HOLD:
                s_held_mask |= o->button_mask;
                s_pc++;
                break;

            case OP_RELEASE:
                if (o->button_mask == 0xFF) s_held_mask = 0;
                else                         s_held_mask &= (uint8_t)~o->button_mask;
                s_pc++;
                break;

            case OP_PRESS: {
                s_held_mask |= o->button_mask;
                int slot = -1;
                for (int i = 0; i < 4; i++) if (!s_press_timers[i].mask) { slot = i; break; }
                if (slot >= 0) {
                    s_press_timers[slot].mask = o->button_mask;
                    s_press_timers[slot].release_frame = frame + o->arg32b;
                }
                s_pc++;
                break;
            }

            case OP_ASSERT_RAM8: {
                uint8_t v = r8 ? r8(o->arg32) : 0;
                if (v != (uint8_t)o->arg32b) {
                    char reason[160];
                    snprintf(reason, sizeof(reason),
                             "input_script line %d: ASSERT_RAM8 $%06X expected $%02X got $%02X",
                             o->line_no, o->arg32, (uint8_t)o->arg32b, v);
                    crash_report_dump_persistent(reason, &g_cpu, o->arg32, 0, frame);
                    exit(3);
                }
                s_pc++;
                break;
            }

            case OP_WAIT_RAM8: {
                uint8_t v = r8 ? r8(o->arg32) : 0;
                if (v != (uint8_t)o->arg32b) return;   /* keep blocking */
                s_pc++;
                break;
            }

            case OP_ASSERT_RAM16: {
                uint16_t v = r16 ? r16(o->arg32) : 0;
                if (v != (uint16_t)o->arg32b) {
                    char reason[160];
                    snprintf(reason, sizeof(reason),
                             "input_script line %d: ASSERT_RAM16 $%06X expected $%04X got $%04X",
                             o->line_no, o->arg32, (uint16_t)o->arg32b, v);
                    crash_report_dump_persistent(reason, &g_cpu, o->arg32, 0, frame);
                    exit(3);
                }
                s_pc++;
                break;
            }

            case OP_WAIT_RAM16: {
                uint16_t v = r16 ? r16(o->arg32) : 0;
                if (v != (uint16_t)o->arg32b) return;
                s_pc++;
                break;
            }

            case OP_WRITE_RAM8:
                if (w8) w8(o->arg32, (uint8_t)o->arg32b);
                fprintf(stderr, "[input_script] WRITE_RAM8 at frame %llu ($%06X=$%02X)\n",
                        (unsigned long long)frame, o->arg32, (uint8_t)o->arg32b);
                s_pc++;
                break;

            case OP_WRITE_RAM16:
                if (w16) w16(o->arg32, (uint16_t)o->arg32b);
                fprintf(stderr, "[input_script] WRITE_RAM16 at frame %llu ($%06X=$%04X)\n",
                        (unsigned long long)frame, o->arg32, (uint16_t)o->arg32b);
                s_pc++;
                break;

            case OP_WRITE_RAM32:
                if (w32) w32(o->arg32, (uint32_t)o->arg32b);
                fprintf(stderr, "[input_script] WRITE_RAM32 at frame %llu ($%06X=$%08X)\n",
                        (unsigned long long)frame, o->arg32, (uint32_t)o->arg32b);
                s_pc++;
                break;

            case OP_SCREENSHOT:
                snprintf(s_pending_screenshot, sizeof(s_pending_screenshot), "%s", o->str);
                /* Wired by the main loop next frame — see input_script_pending_screenshot. */
                fprintf(stderr, "[input_script] SCREENSHOT requested at frame %llu (path=%s)\n",
                        (unsigned long long)frame, o->str);
                s_pc++;
                break;

            case OP_SAVE_STATE:
                snprintf(s_pending_save_state, sizeof(s_pending_save_state), "%s", o->str);
                fprintf(stderr, "[input_script] SAVE_STATE requested at frame %llu (path=%s)\n",
                        (unsigned long long)frame, o->str);
                s_pc++;
                break;

            case OP_LOAD_STATE:
                snprintf(s_pending_load_state, sizeof(s_pending_load_state), "%s", o->str);
                fprintf(stderr, "[input_script] LOAD_STATE requested at frame %llu (path=%s)\n",
                        (unsigned long long)frame, o->str);
                s_pc++;
                break;

            case OP_DUMP_RAM:
                snprintf(s_pending_ram_dump, sizeof(s_pending_ram_dump), "%s", o->str);
                fprintf(stderr, "[input_script] DUMP_RAM requested at frame %llu (path=%s)\n",
                        (unsigned long long)frame, o->str);
                s_pc++;
                break;

            case OP_EXIT:
                s_exit_pending = 1;
                s_exit_code    = (int)o->arg32;
                fprintf(stderr, "[input_script] EXIT %d at frame %llu\n",
                        s_exit_code, (unsigned long long)frame);
                s_pc++;
                return;
        }
    }
}

uint8_t input_script_held_mask(void) { return s_held_mask;     }
bool    input_script_should_exit(void){ return s_exit_pending != 0; }
int     input_script_exit_code  (void){ return s_exit_code;    }
bool    input_script_active     (void){ return s_active != 0;  }

static bool take_pending_path(char *pending, char *out, size_t out_cap)
{
    if (!pending[0])
        return false;
    if (out && out_cap)
        snprintf(out, out_cap, "%s", pending);
    pending[0] = '\0';
    return true;
}

bool input_script_take_save_state(char *out, size_t out_cap)
{
    return take_pending_path(s_pending_save_state, out, out_cap);
}

bool input_script_take_screenshot(char *out, size_t out_cap)
{
    return take_pending_path(s_pending_screenshot, out, out_cap);
}

bool input_script_take_load_state(char *out, size_t out_cap)
{
    return take_pending_path(s_pending_load_state, out, out_cap);
}

bool input_script_take_ram_dump(char *out, size_t out_cap)
{
    return take_pending_path(s_pending_ram_dump, out, out_cap);
}
