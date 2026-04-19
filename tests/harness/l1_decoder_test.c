/*
 * l1_decoder_test.c — L1 decoder-test harness for the 68K recompiler.
 *
 * Reads a fixture file produced by tests/tools/gen_l1_fixtures.py where
 * each row describes one instruction the disasm emits:
 *
 *     <addr_hex> <bytes_hex>\t<mnem>\t<operand_text>
 *
 * For each row the harness runs three checks against the real ROM + our
 * decoder:
 *
 *   L1a (ROM integrity)  : rom bytes at <addr> match <bytes_hex>.
 *                          Catches ROM/disasm drift.
 *   L1b (instruction len): decoder.byte_length == len(bytes_hex)/2.
 *                          Catches decoder length bugs, which cascade.
 *   L1c (mnem class)     : mnemonic class of decoder output matches the
 *                          disasm mnemonic class (Bcc, DBcc, Scc are
 *                          compared as classes, not exact suffixes).
 *
 * Failures are bucketed by "<level>:<expected>-><got>" for triage.
 *
 * Quiet by default: summary to stdout, full failure log to disk.
 * Non-zero exit on any failure.
 *
 * Flags:
 *   --fixture PATH   fixture file (default: tests/fixtures/sonic1/l1/instructions.txt)
 *   --rom PATH       ROM file (default: sonic.bin alongside exe)
 *   --log-dir DIR    (default: tests/last_run)
 *   --filter PAT     only rows whose mnem contains PAT
 *   --bucket N       drill into bucket #N
 *   -v, --verbose    dump every failure to stderr
 *   --summary        single-line summary only
 */
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

#include "m68k_decoder.h"
#include "rom_parser.h"

#define MAX_BUCKETS      256
#define MAX_LINE         512
#define MAX_MNEM          32
#define MAX_OPS          256
#define DEFAULT_FIXTURE  "segagenesisrecomp/tests/fixtures/sonic1/l1/instructions.txt"
#define DEFAULT_ROM      "sonic.bin"
#define DEFAULT_LOG_DIR  "segagenesisrecomp/tests/last_run"

/* ------------- Mnemonic mapping & classes ---------------------------- */

static const char *mnem_string(M68KMnemonic m) {
    switch (m) {
    case MN_OTHER:    return "other";
    case MN_MOVE:     return "move";
    case MN_MOVEQ:    return "moveq";
    case MN_JSR:      return "jsr";
    case MN_BSR:      return "bsr";
    case MN_JMP:      return "jmp";
    case MN_BRA:      return "bra";
    case MN_Bcc:      return "bcc";
    case MN_DBcc:     return "dbcc";
    case MN_RTS:      return "rts";
    case MN_RTE:      return "rte";
    case MN_NOP:      return "nop";
    case MN_STOP:     return "stop";
    case MN_TRAP:     return "trap";
    case MN_MOVEA:    return "movea";
    case MN_MOVEM:    return "movem";
    case MN_LEA:      return "lea";
    case MN_PEA:      return "pea";
    case MN_TST:      return "tst";
    case MN_CLR:      return "clr";
    case MN_NEG:      return "neg";
    case MN_NEGX:     return "negx";
    case MN_NOT:      return "not";
    case MN_EXT:      return "ext";
    case MN_SWAP:     return "swap";
    case MN_ORI:      return "ori";
    case MN_ANDI:     return "andi";
    case MN_SUBI:     return "subi";
    case MN_ADDI:     return "addi";
    case MN_EORI:     return "eori";
    case MN_CMPI:     return "cmpi";
    case MN_ADD:      return "add";
    case MN_ADDA:     return "adda";
    case MN_ADDQ:     return "addq";
    case MN_SUB:      return "sub";
    case MN_SUBA:     return "suba";
    case MN_SUBQ:     return "subq";
    case MN_AND:      return "and";
    case MN_OR:       return "or";
    case MN_EOR:      return "eor";
    case MN_CMP:      return "cmp";
    case MN_CMPA:     return "cmpa";
    case MN_LSL:      return "lsl";
    case MN_LSR:      return "lsr";
    case MN_ASL:      return "asl";
    case MN_ASR:      return "asr";
    case MN_ROL:      return "rol";
    case MN_ROR:      return "ror";
    case MN_ROXL:     return "roxl";
    case MN_ROXR:     return "roxr";
    case MN_Scc:      return "scc";
    case MN_LINK:     return "link";
    case MN_UNLK:     return "unlk";
    case MN_MULS:     return "muls";
    case MN_MULU:     return "mulu";
    case MN_DIVS:     return "divs";
    case MN_DIVU:     return "divu";
    case MN_ABCD:     return "abcd";
    case MN_SBCD:     return "sbcd";
    case MN_BTST:     return "btst";
    case MN_BCHG:     return "bchg";
    case MN_BCLR:     return "bclr";
    case MN_BSET:     return "bset";
    case MN_MOVEP:    return "movep";
    case MN_CHK:      return "chk";
    case MN_NBCD:     return "nbcd";
    case MN_TAS:      return "tas";
    case MN_MOVE_USP: return "move_usp";
    case MN_MOVE_SR:  return "move_sr";
    case MN_MOVE_CCR: return "move_ccr";
    case MN_EXG:      return "exg";
    case MN_ADDX:     return "addx";
    case MN_SUBX:     return "subx";
    }
    return "?";
}

/* Normalize an arbitrary 68K mnemonic string (possibly with size suffix or
 * condition code) to a class string suitable for bucket comparison.
 * Examples:  "move.l" -> "move"; "beq.s" -> "bcc"; "dbne" -> "dbcc";
 *            "seq" -> "scc"; "bra.w" -> "bra".
 */
static void mnem_to_class(const char *mnem, char *out, size_t cap) {
    char buf[MAX_MNEM];
    size_t i = 0;
    while (mnem[i] && i + 1 < sizeof(buf)) {
        buf[i] = (char)tolower((unsigned char)mnem[i]);
        i++;
    }
    buf[i] = '\0';
    /* strip size/scale suffix */
    char *dot = strchr(buf, '.');
    if (dot) *dot = '\0';

    /* move-to/from special registers collapse to "move" for class
     * comparison (disasm source writes "move #imm,sr" as first token
     * "move"; our decoder is strictly more precise). */
    if (strcmp(buf, "move_sr")  == 0 ||
        strcmp(buf, "move_ccr") == 0 ||
        strcmp(buf, "move_usp") == 0) {
        snprintf(out, cap, "%s", "move"); return;
    }

    /* conditional branches -> bcc */
    static const char *bcc_list[] = {
        "beq","bne","bcs","bhs","bcc","bhi","blo","bls",
        "bmi","bpl","bvs","bvc","bge","blt","bgt","ble",
        NULL
    };
    for (const char **p = bcc_list; *p; ++p) {
        if (strcmp(buf, *p) == 0) { snprintf(out, cap, "%s", "bcc"); return; }
    }
    /* DBcc -> dbcc; note dbf/dbra/dbt are also DBcc-class */
    if (strncmp(buf, "db", 2) == 0 && buf[2] != '\0') {
        if (strcmp(buf, "dbra") == 0 || strcmp(buf, "dbf") == 0 ||
            strcmp(buf, "dbt")  == 0) { snprintf(out, cap, "%s", "dbcc"); return; }
        /* any dbxx with a cond */
        const char *rest = buf + 2;
        for (const char **p = bcc_list; *p; ++p) {
            /* Compare "eq" vs tail of "beq" */
            if (strcmp(rest, (*p) + 1) == 0) {
                snprintf(out, cap, "%s", "dbcc"); return;
            }
        }
    }
    /* Scc: s<cc> where cc is a condition code (includes sf/st) */
    if (buf[0] == 's' && buf[1] != '\0') {
        static const char *scc_list[] = {
            "seq","sne","scs","shs","scc","shi","slo","sls",
            "smi","spl","svs","svc","sge","slt","sgt","sle",
            "sf","st",
            NULL
        };
        for (const char **p = scc_list; *p; ++p) {
            if (strcmp(buf, *p) == 0) { snprintf(out, cap, "%s", "scc"); return; }
        }
    }

    snprintf(out, cap, "%s", buf);
}

/* ------------- Bucket recorder --------------------------------------- */

typedef struct {
    char     key[80];        /* "<level>:<expected>-><got>" */
    unsigned count;
    uint32_t first_addr;
    char     first_expected[MAX_MNEM];
    char     first_got[MAX_MNEM];
    char     first_bytes[24];
} Bucket;

static Bucket g_buckets[MAX_BUCKETS];
static int    g_num_buckets = 0;

static Bucket *bucket_find_or_create(const char *key) {
    for (int i = 0; i < g_num_buckets; ++i) {
        if (strcmp(g_buckets[i].key, key) == 0) return &g_buckets[i];
    }
    if (g_num_buckets >= MAX_BUCKETS) return &g_buckets[MAX_BUCKETS - 1];
    Bucket *b = &g_buckets[g_num_buckets++];
    memset(b, 0, sizeof(*b));
    snprintf(b->key, sizeof(b->key), "%s", key);
    return b;
}

static int bucket_cmp_desc(const void *a, const void *b) {
    const Bucket *x = (const Bucket *)a, *y = (const Bucket *)b;
    if (x->count > y->count) return -1;
    if (x->count < y->count) return 1;
    return 0;
}

static void record_failure(const char *level, const char *expected,
                           const char *got, uint32_t addr,
                           const char *bytes_hex, FILE *flog) {
    char key[80];
    snprintf(key, sizeof(key), "%s:%s->%s", level, expected, got);
    Bucket *b = bucket_find_or_create(key);
    if (b->count == 0) {
        b->first_addr = addr;
        snprintf(b->first_expected, sizeof(b->first_expected), "%s", expected);
        snprintf(b->first_got,      sizeof(b->first_got),      "%s", got);
        snprintf(b->first_bytes,    sizeof(b->first_bytes),    "%s", bytes_hex);
    }
    b->count++;
    if (flog) {
        fprintf(flog, "0x%06x [%s] %s\n  expected: %s\n  got:      %s\n",
                addr, level, bytes_hex, expected, got);
    }
}

/* ------------- Fixture parsing --------------------------------------- */

typedef struct {
    uint32_t addr;
    uint8_t  bytes[16];
    uint8_t  nbytes;
    char     mnem[MAX_MNEM];
    char     ops[MAX_OPS];
} Row;

static int parse_hex_byte(const char *s, uint8_t *out) {
    if (!isxdigit((unsigned char)s[0]) || !isxdigit((unsigned char)s[1]))
        return 0;
    char tmp[3] = { s[0], s[1], 0 };
    *out = (uint8_t)strtoul(tmp, NULL, 16);
    return 1;
}

/* "<addr> <bytes_hex>\t<mnem>\t<ops>" */
static int parse_row(const char *line, Row *r) {
    unsigned addr = 0;
    int pos = 0;
    if (sscanf(line, "%x %n", &addr, &pos) != 1) return 0;
    r->addr = (uint32_t)addr;

    const char *p = line + pos;
    int nb = 0;
    while (nb < 16 && isxdigit((unsigned char)p[0]) && isxdigit((unsigned char)p[1])) {
        if (!parse_hex_byte(p, &r->bytes[nb])) return 0;
        p += 2;
        ++nb;
    }
    r->nbytes = (uint8_t)nb;
    if (nb < 2 || (nb & 1)) return 0;

    /* Skip whitespace / tab. */
    while (*p == ' ' || *p == '\t') ++p;

    /* Mnem up to next tab or end. */
    size_t mi = 0;
    while (*p && *p != '\t' && *p != '\n' && mi + 1 < sizeof(r->mnem)) {
        r->mnem[mi++] = *p++;
    }
    r->mnem[mi] = '\0';
    while (*p == '\t') ++p;

    size_t oi = 0;
    while (*p && *p != '\n' && *p != '\r' && oi + 1 < sizeof(r->ops)) {
        r->ops[oi++] = *p++;
    }
    r->ops[oi] = '\0';
    return r->mnem[0] ? 1 : 0;
}

/* ------------- Options ---------------------------------------------- */

typedef struct {
    const char *fixture;
    const char *rom_path;
    const char *log_dir;
    const char *filter;
    int         verbose;
    int         summary_only;
    int         drill_bucket;
} Options;

static int ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) return 0;
#ifdef _WIN32
    return _mkdir(path);
#else
    return mkdir(path, 0755);
#endif
}

/* ------------- Main runner ------------------------------------------ */

static int run(const Options *opt) {
    GenesisRom rom;
    if (!rom_parse(opt->rom_path, &rom)) {
        fprintf(stderr, "l1: cannot parse ROM %s\n", opt->rom_path);
        return 2;
    }

    FILE *f = fopen(opt->fixture, "r");
    if (!f) {
        fprintf(stderr, "l1: cannot open fixture %s: %s\n",
                opt->fixture, strerror(errno));
        rom_free(&rom);
        return 2;
    }

    ensure_dir(opt->log_dir);
    char fail_path[512];
    snprintf(fail_path, sizeof(fail_path), "%s/l1_failures.log", opt->log_dir);
    FILE *flog = fopen(fail_path, "w");

    size_t total = 0, passed = 0, filtered = 0;
    size_t fail_bytes = 0, fail_len = 0, fail_mnem = 0;

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        Row r;
        if (!parse_row(line, &r)) continue;
        if (opt->filter && !strstr(r.mnem, opt->filter)) {
            ++filtered;
            continue;
        }
        ++total;

        bool row_failed = false;
        char bytes_hex[48];
        {
            size_t bi = 0;
            for (int i = 0; i < r.nbytes && bi + 2 < sizeof(bytes_hex); ++i) {
                bi += (size_t)snprintf(bytes_hex + bi, sizeof(bytes_hex) - bi,
                                       "%02x", r.bytes[i]);
            }
        }

        /* L1a: ROM integrity at <addr>. */
        bool bytes_ok = true;
        for (int i = 0; i < r.nbytes; ++i) {
            uint8_t got = rom_read8(&rom, r.addr + (uint32_t)i);
            if (got != r.bytes[i]) { bytes_ok = false; break; }
        }
        if (!bytes_ok) {
            char got_bytes[48];
            size_t bi = 0;
            for (int i = 0; i < r.nbytes && bi + 2 < sizeof(got_bytes); ++i) {
                uint8_t g = rom_read8(&rom, r.addr + (uint32_t)i);
                bi += (size_t)snprintf(got_bytes + bi, sizeof(got_bytes) - bi,
                                       "%02x", g);
            }
            record_failure("bytes", bytes_hex, got_bytes, r.addr, bytes_hex, flog);
            ++fail_bytes;
            row_failed = true;
        }

        /* L1b + L1c: decode. */
        M68KInstr insn;
        if (!m68k_decode(&rom, r.addr, &insn)) {
            record_failure("decode", r.mnem, "failed", r.addr, bytes_hex, flog);
            ++fail_len;
            row_failed = true;
        } else {
            /* Length check. */
            if (insn.byte_length != r.nbytes) {
                char exp[16], got[16];
                snprintf(exp, sizeof(exp), "%u", (unsigned)r.nbytes);
                snprintf(got, sizeof(got), "%u", (unsigned)insn.byte_length);
                record_failure("len", exp, got, r.addr, bytes_hex, flog);
                ++fail_len;
                row_failed = true;
            }
            /* Mnem class check. */
            char exp_class[MAX_MNEM], got_class[MAX_MNEM];
            mnem_to_class(r.mnem, exp_class, sizeof(exp_class));
            mnem_to_class(mnem_string(insn.mnemonic), got_class, sizeof(got_class));
            if (strcmp(exp_class, got_class) != 0) {
                record_failure("mnem", exp_class, got_class, r.addr, bytes_hex, flog);
                ++fail_mnem;
                row_failed = true;
            }
        }

        if (!row_failed) ++passed;
        else if (opt->verbose) {
            fprintf(stderr, "FAIL 0x%06x %s : %s %s\n",
                    r.addr, bytes_hex, r.mnem, r.ops);
        }
    }
    fclose(f);
    if (flog) fclose(flog);
    rom_free(&rom);

    size_t fail_rows = total - passed;

    /* Summary file */
    char sum_path[512];
    snprintf(sum_path, sizeof(sum_path), "%s/l1_summary.txt", opt->log_dir);
    FILE *fs = fopen(sum_path, "w");
    if (fs) {
        fprintf(fs, "L1 decoder: %zu/%zu ok  (bytes=%zu len=%zu mnem=%zu, %d buckets)\n",
                passed, total, fail_bytes, fail_len, fail_mnem, g_num_buckets);
        fclose(fs);
    }

    if (opt->summary_only) {
        if (fail_rows == 0) printf("L1: %zu/%zu ok\n", passed, total);
        else printf("L1: %zu/%zu ok, %zu rows failed (bytes=%zu len=%zu mnem=%zu)\n",
                    passed, total, fail_rows, fail_bytes, fail_len, fail_mnem);
        return fail_rows ? 1 : 0;
    }

    if (fail_rows == 0) {
        printf("L1 decoder: %zu/%zu ok\n", passed, total);
        return 0;
    }

    qsort(g_buckets, (size_t)g_num_buckets, sizeof(Bucket), bucket_cmp_desc);

    if (opt->drill_bucket > 0 && opt->drill_bucket <= g_num_buckets) {
        Bucket *b = &g_buckets[opt->drill_bucket - 1];
        printf("bucket %d: %s  (%u rows)\n", opt->drill_bucket, b->key, b->count);
        printf("first:  0x%06x  bytes=%s  expected=%s  got=%s\n",
               b->first_addr, b->first_bytes, b->first_expected, b->first_got);
        printf("full log: %s\n", fail_path);
        return 1;
    }

    printf("L1 decoder: %zu/%zu ok  (bytes=%zu len=%zu mnem=%zu, %d buckets)\n",
           passed, total, fail_bytes, fail_len, fail_mnem, g_num_buckets);
    int show = g_num_buckets < 10 ? g_num_buckets : 10;
    for (int i = 0; i < show; ++i) {
        Bucket *b = &g_buckets[i];
        printf("  [%2d %6ux] %-32s first 0x%06x %s  expected=%s got=%s\n",
               i + 1, b->count, b->key, b->first_addr, b->first_bytes,
               b->first_expected, b->first_got);
    }
    if (g_num_buckets > show)
        printf("  ... %d more buckets\n", g_num_buckets - show);
    printf("full log: %s\n", fail_path);
    return 1;
}

int main(int argc, char **argv) {
    Options opt = {
        .fixture      = DEFAULT_FIXTURE,
        .rom_path     = DEFAULT_ROM,
        .log_dir      = DEFAULT_LOG_DIR,
        .filter       = NULL,
        .verbose      = 0,
        .summary_only = 0,
        .drill_bucket = 0,
    };
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (!strcmp(a, "-v") || !strcmp(a, "--verbose")) opt.verbose = 1;
        else if (!strcmp(a, "--summary")) opt.summary_only = 1;
        else if (!strcmp(a, "--fixture") && i + 1 < argc) opt.fixture = argv[++i];
        else if (!strcmp(a, "--rom")     && i + 1 < argc) opt.rom_path = argv[++i];
        else if (!strcmp(a, "--log-dir") && i + 1 < argc) opt.log_dir = argv[++i];
        else if (!strcmp(a, "--filter")  && i + 1 < argc) opt.filter = argv[++i];
        else if (!strcmp(a, "--bucket")  && i + 1 < argc) opt.drill_bucket = atoi(argv[++i]);
        else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            fprintf(stderr,
                "usage: l1_decoder_test [--fixture PATH] [--rom PATH]\n"
                "                       [--log-dir DIR] [--filter PAT]\n"
                "                       [--bucket N] [-v|--verbose] [--summary]\n");
            return 0;
        } else {
            fprintf(stderr, "unknown arg: %s\n", a);
            return 2;
        }
    }
    return run(&opt);
}
