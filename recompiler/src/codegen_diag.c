/*
 * codegen_diag.c — centralized diagnostics for codegen coverage gaps.
 */
#include "codegen_diag.h"
#include <stdlib.h>
#include <string.h>

static int s_counts[CGD_KIND_COUNT];

#define EVENT_CAP_MAX 4096
static CodegenDiagEvent s_events[EVENT_CAP_MAX];
static int s_event_count = 0;

static const char *s_kind_names[CGD_KIND_COUNT] = {
    "MN_OTHER",
    "TODO_ADDX_MEM_PREDEC",
    "TODO_SUBX_MEM_PREDEC",
    "TODO_DYNAMIC_JSR_UNSUPPORTED",
    "TODO_DYNAMIC_JMP_UNSUPPORTED",
    "BRANCH_WITHOUT_TARGET",
    "INVALID_STORE_EA",
    "MOVE_CCR_DIRECTION_AMBIGUOUS",
};

void codegen_diag_reset(void) {
    memset(s_counts, 0, sizeof(s_counts));
    s_event_count = 0;
}

void codegen_diag_record(CodegenDiagKind kind, uint32_t addr, uint16_t opcode,
                         M68KMnemonic mn, const char *func_name, uint32_t func_addr) {
    if ((unsigned)kind >= CGD_KIND_COUNT) return;
    s_counts[kind]++;
    if (s_event_count < EVENT_CAP_MAX) {
        CodegenDiagEvent *e = &s_events[s_event_count++];
        e->kind      = kind;
        e->addr      = addr;
        e->opcode    = opcode;
        e->mnemonic  = mn;
        e->func_name = func_name;
        e->func_addr = func_addr;
    }
}

int codegen_diag_total(void) {
    int total = 0;
    for (int i = 0; i < CGD_KIND_COUNT; i++) total += s_counts[i];
    return total;
}

int codegen_diag_count(CodegenDiagKind kind) {
    if ((unsigned)kind >= CGD_KIND_COUNT) return 0;
    return s_counts[kind];
}

int codegen_diag_event_count(void) {
    return s_event_count;
}

const CodegenDiagEvent *codegen_diag_get(int i) {
    if (i < 0 || i >= s_event_count) return NULL;
    return &s_events[i];
}

const char *codegen_diag_kind_str(CodegenDiagKind kind) {
    if ((unsigned)kind >= CGD_KIND_COUNT) return "?";
    return s_kind_names[kind];
}

void codegen_diag_print_summary(FILE *out) {
    if (!out) return;
    int total = codegen_diag_total();

    fprintf(out, "[Codegen] Unsupported summary:\n");
    for (int k = 0; k < CGD_KIND_COUNT; k++) {
        fprintf(out, "  %-32s %d\n", s_kind_names[k], s_counts[k]);
    }
    fprintf(out, "  %-32s %d\n", "TOTAL", total);

    if (total == 0) return;

    /* Print up to 16 representative events, grouped by kind, so the
     * user can see ROM PC + opcode + function context without dumping
     * thousands of lines. */
    fprintf(out, "[Codegen] First events (up to 16):\n");
    int shown = 0;
    for (int i = 0; i < s_event_count && shown < 16; i++) {
        const CodegenDiagEvent *e = &s_events[i];
        fprintf(out, "    %-32s @ $%06X opcode=$%04X func=%s($%06X)\n",
                s_kind_names[e->kind], e->addr, e->opcode,
                e->func_name ? e->func_name : "<unnamed>",
                e->func_addr);
        shown++;
    }
}
