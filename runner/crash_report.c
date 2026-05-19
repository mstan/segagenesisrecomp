/*
 * crash_report.c — structured crash diagnostic dump.
 *
 * Lazy-loaded symbol table from a flat addr,name CSV plus two
 * power-of-two ring buffers for the rdb_on_block / rdb_on_func
 * trail. The watchdog (and any other unrecoverable-stuck path)
 * calls crash_report_dump() to emit a single multi-line report.
 *
 * Symbol lookup is binary-search over a sorted array. The CSV is
 * read once; entries with a name (label) become symbol rows, the
 * notes column is ignored. Garbled rows are skipped silently —
 * the report degrades gracefully to raw $XXXXXX.
 *
 * The ring recorders are intentionally branch-free on the hot
 * path (just a write+increment) so leaving them on in release
 * builds is cheap.
 */

#include "crash_report.h"
#include "genesis_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

/* ---- Ring buffer ---- */

#define BLOCK_RING_SIZE  64u   /* must be power of two */

static uint32_t s_block_ring[BLOCK_RING_SIZE];
static uint32_t s_block_head;       /* next write slot, monotonic */
static uint32_t s_block_total;

void crash_report_record_block(uint32_t block_addr) {
    s_block_ring[s_block_head & (BLOCK_RING_SIZE - 1)] = block_addr;
    s_block_head++;
    s_block_total++;
}

/* ---- Persistent crash log ---- */

static char s_log_path[256] = "last_error.log";
static int  s_log_disabled  = 0;

void crash_report_set_log_path(const char *path) {
    if (!path) {
        s_log_disabled = 1;
        return;
    }
    s_log_disabled = 0;
    snprintf(s_log_path, sizeof(s_log_path), "%s", path);
}

/* ---- Symbol table ---- */

typedef struct { uint32_t addr; char *name; } CrSymbol;

static CrSymbol *s_symbols = NULL;
static int       s_symbol_count = 0;

static int symbol_cmp(const void *a, const void *b) {
    uint32_t aa = ((const CrSymbol *)a)->addr;
    uint32_t bb = ((const CrSymbol *)b)->addr;
    return (aa < bb) ? -1 : (aa > bb) ? 1 : 0;
}

int crash_report_load_symbols(const char *csv_path) {
    if (!csv_path) return 0;
    FILE *f = fopen(csv_path, "r");
    if (!f) return 0;

    /* Reset any prior table. */
    for (int i = 0; i < s_symbol_count; i++) free(s_symbols[i].name);
    free(s_symbols);
    s_symbols = NULL;
    s_symbol_count = 0;

    int cap = 0;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        /* Skip blank, comment, and the CSV header row if present. */
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '\n' || *p == '\r' || *p == '#') continue;
        if (strncmp(p, "addr,", 5) == 0 || strncmp(p, "address,", 8) == 0) continue;

        /* Parse "addr,name[,notes]". addr may be hex or decimal. */
        char *comma = strchr(p, ',');
        if (!comma) continue;
        *comma = '\0';
        char *addr_str = p;
        char *name_str = comma + 1;

        /* Strip optional 0x / 0X prefix; strtoul with base 16 handles both. */
        uint32_t addr = (uint32_t)strtoul(addr_str, NULL, 16);
        if (addr == 0 && addr_str[0] != '0') continue;  /* parse error */

        /* Truncate name at second comma (notes field) and trim trailing whitespace. */
        char *name_comma = strchr(name_str, ',');
        if (name_comma) *name_comma = '\0';
        char *end = name_str + strlen(name_str);
        while (end > name_str && (end[-1] == '\n' || end[-1] == '\r' ||
                                  end[-1] == ' '  || end[-1] == '\t')) {
            *--end = '\0';
        }
        if (*name_str == '\0') continue;

        if (s_symbol_count >= cap) {
            cap = cap ? cap * 2 : 1024;
            CrSymbol *p2 = realloc(s_symbols, (size_t)cap * sizeof(CrSymbol));
            if (!p2) { fclose(f); return s_symbol_count; }
            s_symbols = p2;
        }
        s_symbols[s_symbol_count].addr = addr;
        s_symbols[s_symbol_count].name = strdup(name_str);
        if (!s_symbols[s_symbol_count].name) { fclose(f); return s_symbol_count; }
        s_symbol_count++;
    }
    fclose(f);

    qsort(s_symbols, (size_t)s_symbol_count, sizeof(CrSymbol), symbol_cmp);
    return s_symbol_count;
}

const char *crash_report_lookup(uint32_t addr) {
    if (s_symbol_count == 0) return NULL;
    /* Find largest entry with addr <= query. Binary-search lower bound,
     * then return the predecessor if it's a near-match (within 256
     * bytes — enough to land in the same function for typical Sonic
     * function lengths, without false-matching across data tables). */
    int lo = 0, hi = s_symbol_count;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (s_symbols[mid].addr <= addr) lo = mid + 1;
        else                              hi = mid;
    }
    if (lo == 0) return NULL;
    const CrSymbol *cand = &s_symbols[lo - 1];
    if (cand->addr == addr)        return cand->name;
    if (addr - cand->addr <= 0x100) return cand->name;  /* nearby */
    return NULL;
}

/* Helper: format "$XXXXXX SymbolName+offset" or "$XXXXXX (no symbol)" into buf. */
static void format_addr(char *buf, size_t bufsize, uint32_t addr) {
    if (s_symbol_count == 0) {
        snprintf(buf, bufsize, "$%06X", addr);
        return;
    }
    int lo = 0, hi = s_symbol_count;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (s_symbols[mid].addr <= addr) lo = mid + 1;
        else                              hi = mid;
    }
    if (lo == 0) {
        snprintf(buf, bufsize, "$%06X", addr);
        return;
    }
    const CrSymbol *cand = &s_symbols[lo - 1];
    uint32_t delta = addr - cand->addr;
    if (delta == 0)            snprintf(buf, bufsize, "$%06X %s", addr, cand->name);
    else if (delta <= 0x200)   snprintf(buf, bufsize, "$%06X %s+%u", addr, cand->name, delta);
    else                       snprintf(buf, bufsize, "$%06X", addr);
}

/* Read a 32-bit big-endian longword from g_ram at offset `off` (0..0xFFFC).
 * Used by the stack walk — A7 is in 68K-RAM ($FFxxxx mirror). Bounds-checked. */
extern uint8_t g_ram[0x10000];
static uint32_t ram_read32_be(uint32_t off) {
    if (off + 3 >= 0x10000) return 0;
    return  ((uint32_t)g_ram[off]     << 24) |
            ((uint32_t)g_ram[off + 1] << 16) |
            ((uint32_t)g_ram[off + 2] <<  8) |
             (uint32_t)g_ram[off + 3];
}

/* Plausibility test for "this longword is a return address". Sonic is a
 * 4MB-or-less Genesis cart, so any address in [0x000200, 0x400000] is a
 * candidate. Filter out 0x00000000 / 0xFFFFFFFF / odd addresses. */
static int looks_like_code_addr(uint32_t v) {
    if (v == 0u || v == 0xFFFFFFFFu) return 0;
    if (v & 1u) return 0;
    v &= 0xFFFFFFu;
    return (v >= 0x000200u && v < 0x400000u);
}

void crash_report_dump(FILE *out, const char *reason,
                       const M68KState *cpu,
                       uint32_t last_access_addr,
                       int last_access_is_write,
                       uint64_t frame_count)
{
    char fbuf[128];

    fprintf(out, "\n");
    fprintf(out, "=========================================================================\n");
    fprintf(out, "  CRASH REPORT  frame=%llu  reason: %s\n",
            (unsigned long long)frame_count, reason ? reason : "(unspecified)");
    fprintf(out, "=========================================================================\n");

    /* ---- Last bus access ---- */
    fprintf(out, "  Last access: %s $%06X\n",
            last_access_is_write ? "WRITE" : "READ", last_access_addr);

    /* ---- Registers ---- */
    if (cpu) {
        fprintf(out, "\n  Registers:\n");
        fprintf(out, "    D0=$%08X D1=$%08X D2=$%08X D3=$%08X\n",
                cpu->D[0], cpu->D[1], cpu->D[2], cpu->D[3]);
        fprintf(out, "    D4=$%08X D5=$%08X D6=$%08X D7=$%08X\n",
                cpu->D[4], cpu->D[5], cpu->D[6], cpu->D[7]);
        fprintf(out, "    A0=$%08X A1=$%08X A2=$%08X A3=$%08X\n",
                cpu->A[0], cpu->A[1], cpu->A[2], cpu->A[3]);
        fprintf(out, "    A4=$%08X A5=$%08X A6=$%08X A7=$%08X (SP)\n",
                cpu->A[4], cpu->A[5], cpu->A[6], cpu->A[7]);
        fprintf(out, "    SR=$%04X\n", cpu->SR);
    }

    /* ---- Stack walk ---- */
    if (cpu) {
        fprintf(out, "\n  Stack walk (A7=$%06X, scanning for return addresses):\n",
                cpu->A[7] & 0xFFFFFFu);
        uint32_t sp = cpu->A[7] & 0xFFFFu;   /* RAM-relative offset */
        int found = 0;
        for (uint32_t off = 0; off < 0x100 && off + sp < 0x10000; off += 2) {
            uint32_t v = ram_read32_be((sp + off) & 0xFFFC);
            if (looks_like_code_addr(v)) {
                format_addr(fbuf, sizeof(fbuf), v & 0xFFFFFFu);
                fprintf(out, "    [SP+$%03X] %s\n", off, fbuf);
                if (++found >= 12) break;
            }
        }
        if (!found) fprintf(out, "    (no plausible return addresses found in top 256 bytes)\n");
    }

    /* ---- Recent block transitions ---- */
    if (s_block_total > 0) {
        uint32_t window = (s_block_total < BLOCK_RING_SIZE) ? s_block_total : BLOCK_RING_SIZE;
        fprintf(out, "\n  Recent execution trail (last %u of %u block transitions):\n",
                window, s_block_total);
        for (uint32_t i = 0; i < window; i++) {
            uint32_t idx = (s_block_head - window + i) & (BLOCK_RING_SIZE - 1);
            format_addr(fbuf, sizeof(fbuf), s_block_ring[idx]);
            fprintf(out, "    [%2u] %s\n", i, fbuf);
        }
    } else {
        fprintf(out, "\n  Recent execution trail: (empty — rdb_on_block hooks not firing;\n");
        fprintf(out, "                            regen with --reverse-debug to populate)\n");
    }

    /* ---- Symbol-table state ---- */
    fprintf(out, "\n  Symbol table: %d entries loaded%s\n",
            s_symbol_count,
            s_symbol_count == 0 ? "  (call crash_report_load_symbols at startup)" : "");

    fprintf(out, "=========================================================================\n");
}

void crash_report_dump_persistent(const char *reason,
                                  const M68KState *cpu,
                                  uint32_t last_access_addr,
                                  int last_access_is_write,
                                  uint64_t frame_count)
{
    /* Always emit to stderr first so live observers see it. */
    crash_report_dump(stderr, reason, cpu, last_access_addr,
                      last_access_is_write, frame_count);
    fflush(stderr);

    if (s_log_disabled) return;

    /* Append to the persistent log so the most-recent crash is
     * preserved even if stderr was redirected, piped through head,
     * or otherwise eaten. Open in append mode so multiple runs
     * accumulate (pruned externally via log rotation if needed). */
    FILE *lf = fopen(s_log_path, "a");
    if (!lf) return;
    /* Lead with a separator + ISO-style timestamp so distinct crash
     * sessions are easy to spot when grepping the log. */
    {
        time_t now = time(NULL);
        struct tm *tm_now = localtime(&now);
        char ts[32] = "(no time)";
        if (tm_now) strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm_now);
        fprintf(lf, "\n\n========== crash @ %s ==========\n", ts);
    }
    crash_report_dump(lf, reason, cpu, last_access_addr,
                      last_access_is_write, frame_count);
    fclose(lf);
}
