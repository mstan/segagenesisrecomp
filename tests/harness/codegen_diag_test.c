/*
 * codegen_diag_test.c — unit smoke test for the codegen diagnostics module.
 *
 * Drives codegen_diag directly without spinning up a full ROM / codegen
 * pipeline. Exercises:
 *   - reset clears state
 *   - record bumps the per-kind count
 *   - total sums all per-kind counts
 *   - event log preserves insertion order with metadata
 *   - kind_str returns sane strings
 *   - print_summary writes a non-empty buffer
 *
 * Non-zero exit on first assertion failure.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codegen_diag.h"

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, msg); \
        g_failures++; \
    } \
} while (0)

int main(void) {
    /* Fresh state */
    codegen_diag_reset();
    CHECK(codegen_diag_total() == 0, "reset → total 0");
    CHECK(codegen_diag_event_count() == 0, "reset → events 0");

    /* Single record */
    codegen_diag_record(CGD_BRANCH_WITHOUT_TARGET, 0x001234, 0x6000, MN_BRA,
                        "TestFunc", 0x001000);
    CHECK(codegen_diag_count(CGD_BRANCH_WITHOUT_TARGET) == 1, "branch count = 1");
    CHECK(codegen_diag_total() == 1, "total = 1");

    const CodegenDiagEvent *e0 = codegen_diag_get(0);
    CHECK(e0 != NULL, "get(0) non-null");
    CHECK(e0->kind == CGD_BRANCH_WITHOUT_TARGET, "event kind preserved");
    CHECK(e0->addr == 0x001234, "event addr preserved");
    CHECK(e0->opcode == 0x6000, "event opcode preserved");
    CHECK(e0->mnemonic == MN_BRA, "event mnemonic preserved");
    CHECK(e0->func_name && strcmp(e0->func_name, "TestFunc") == 0,
          "event func_name preserved");
    CHECK(e0->func_addr == 0x001000, "event func_addr preserved");

    /* Multiple records of mixed kinds */
    codegen_diag_record(CGD_TODO_DYNAMIC_JSR_UNSUPPORTED, 0x002000, 0x4E90,
                        MN_JSR, NULL, 0);
    codegen_diag_record(CGD_TODO_DYNAMIC_JSR_UNSUPPORTED, 0x002004, 0x4E90,
                        MN_JSR, NULL, 0);
    codegen_diag_record(CGD_INVALID_STORE_EA, 0x003000, 0x4280,
                        MN_CLR, "ClrFunc", 0x002F00);

    CHECK(codegen_diag_count(CGD_TODO_DYNAMIC_JSR_UNSUPPORTED) == 2,
          "dyn JSR count = 2");
    CHECK(codegen_diag_count(CGD_INVALID_STORE_EA) == 1, "invalid store = 1");
    CHECK(codegen_diag_count(CGD_BRANCH_WITHOUT_TARGET) == 1, "branch still 1");
    CHECK(codegen_diag_total() == 4, "total = 4");
    CHECK(codegen_diag_event_count() == 4, "events = 4");

    /* Order preserved */
    CHECK(codegen_diag_get(1)->kind == CGD_TODO_DYNAMIC_JSR_UNSUPPORTED,
          "event 1 kind");
    CHECK(codegen_diag_get(2)->kind == CGD_TODO_DYNAMIC_JSR_UNSUPPORTED,
          "event 2 kind");
    CHECK(codegen_diag_get(3)->kind == CGD_INVALID_STORE_EA, "event 3 kind");

    /* Reset clears everything */
    codegen_diag_reset();
    CHECK(codegen_diag_total() == 0, "post-reset total = 0");
    CHECK(codegen_diag_event_count() == 0, "post-reset events = 0");
    CHECK(codegen_diag_count(CGD_BRANCH_WITHOUT_TARGET) == 0,
          "post-reset branch = 0");

    /* Kind names */
    for (int k = 0; k < CGD_KIND_COUNT; k++) {
        const char *name = codegen_diag_kind_str((CodegenDiagKind)k);
        CHECK(name && name[0], "kind name non-empty");
    }

    /* Summary writes something non-trivial when there are events */
    codegen_diag_record(CGD_BRANCH_WITHOUT_TARGET, 0x004000, 0x6000, MN_BRA,
                        "BranchFunc", 0x003F00);
    char buf[4096] = {0};
    FILE *mem = NULL;
#ifdef _WIN32
    /* No POSIX fmemopen on MSVC; use a real tempfile. */
    mem = tmpfile();
    CHECK(mem != NULL, "tmpfile open");
    if (mem) {
        codegen_diag_print_summary(mem);
        fseek(mem, 0, SEEK_SET);
        size_t n = fread(buf, 1, sizeof(buf) - 1, mem);
        buf[n] = 0;
        fclose(mem);
        CHECK(strstr(buf, "BRANCH_WITHOUT_TARGET") != NULL,
              "summary mentions BRANCH_WITHOUT_TARGET");
        CHECK(strstr(buf, "TOTAL") != NULL, "summary has TOTAL");
    }
#else
    mem = fmemopen(buf, sizeof(buf), "w");
    CHECK(mem != NULL, "fmemopen");
    if (mem) {
        codegen_diag_print_summary(mem);
        fclose(mem);
        CHECK(strstr(buf, "BRANCH_WITHOUT_TARGET") != NULL,
              "summary mentions BRANCH_WITHOUT_TARGET");
        CHECK(strstr(buf, "TOTAL") != NULL, "summary has TOTAL");
    }
#endif

    if (g_failures == 0) {
        printf("codegen_diag: all checks passed\n");
        return 0;
    }
    fprintf(stderr, "codegen_diag: %d check(s) failed\n", g_failures);
    return 1;
}
