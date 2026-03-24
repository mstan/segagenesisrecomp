/*
 * input_script.c — Deterministic input script player.
 *
 * Loads a text script and replays button presses frame by frame.
 * Used for automated testing and screenshot capture.
 *
 * Script command reference:
 *   WAIT <n>                       — wait n frames
 *   HOLD <BTN>                     — hold button (A B C START UP DOWN LEFT RIGHT)
 *   RELEASE <BTN>                  — release button
 *   TURBO ON|OFF                   — toggle fast-forward
 *   SCREENSHOT [file]              — save PNG to C:/temp/
 *   LOG <msg>                      — print message to stdout
 *   SAVE_STATE <path>              — save state to file
 *   LOAD_STATE <path>              — restore state from file
 *   WAIT_RAM16 <hex_addr> <hex_val>— block until g_ram[addr]==val (30s timeout)
 *   ASSERT_RAM16 <hex_addr> <hex_val> [msg] — assert RAM value
 *   EXIT [code]                    — exit with code (default 0)
 *
 * TODO: implement script execution in the main loop.
 */
#include "input_script.h"
#include "genesis_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SCRIPT_LINES 4096
#define MAX_LINE_LEN     256

typedef struct {
    char line[MAX_LINE_LEN];
} ScriptLine;

static ScriptLine s_lines[MAX_SCRIPT_LINES];
static int        s_line_count  = 0;
static int        s_current_line = 0;
static int        s_wait_frames  = 0;

static uint8_t button_mask(const char *name) {
    /* Genesis 3-button: A=bit4, B=bit4(TH low), C=bit5, Start=bit5(TH low) */
    /* Simplified: use NES-style bit layout for now */
    if (strcmp(name, "A")     == 0) return 0x10;
    if (strcmp(name, "B")     == 0) return 0x20;
    if (strcmp(name, "C")     == 0) return 0x40;
    if (strcmp(name, "START") == 0) return 0x80;
    if (strcmp(name, "UP")    == 0) return 0x01;
    if (strcmp(name, "DOWN")  == 0) return 0x02;
    if (strcmp(name, "LEFT")  == 0) return 0x04;
    if (strcmp(name, "RIGHT") == 0) return 0x08;
    return 0;
}

bool input_script_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return false;

    s_line_count   = 0;
    s_current_line = 0;
    s_wait_frames  = 0;

    while (s_line_count < MAX_SCRIPT_LINES &&
           fgets(s_lines[s_line_count].line, MAX_LINE_LEN, f)) {
        /* Strip newline */
        int len = (int)strlen(s_lines[s_line_count].line);
        while (len > 0 && (s_lines[s_line_count].line[len-1] == '\n' ||
                            s_lines[s_line_count].line[len-1] == '\r'))
            s_lines[s_line_count].line[--len] = '\0';
        if (len > 0) s_line_count++;
    }
    fclose(f);
    printf("[InputScript] Loaded %d commands from %s\n", s_line_count, path);
    return true;
}

void input_script_tick(void) {
    /* Called once per frame from the main loop */
    if (s_wait_frames > 0) { s_wait_frames--; return; }
    if (s_current_line >= s_line_count) return;

    char *line = s_lines[s_current_line++].line;
    char cmd[32] = {0}, arg1[128] = {0}, arg2[128] = {0};
    sscanf(line, "%31s %127s %127s", cmd, arg1, arg2);

    if (strcmp(cmd, "WAIT") == 0) {
        s_wait_frames = atoi(arg1) - 1;
    } else if (strcmp(cmd, "HOLD") == 0) {
        g_controller1_buttons |= button_mask(arg1);
    } else if (strcmp(cmd, "RELEASE") == 0) {
        g_controller1_buttons &= ~button_mask(arg1);
    } else if (strcmp(cmd, "LOG") == 0) {
        printf("[Script] %s %s\n", arg1, arg2);
    } else if (strcmp(cmd, "EXIT") == 0) {
        exit(arg1[0] ? atoi(arg1) : 0);
    }
    /* TODO: SCREENSHOT, SAVE_STATE, LOAD_STATE, WAIT_RAM16, ASSERT_RAM16, TURBO */
}

bool input_script_done(void) {
    return s_current_line >= s_line_count && s_wait_frames <= 0;
}
