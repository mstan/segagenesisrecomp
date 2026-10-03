/*
 * runtime_evidence.c — persistent, build-stamped runtime evidence files.
 * See runtime_evidence.h for the lifecycle and file format.
 */
#include "runtime_evidence.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#if defined(__APPLE__) || defined(__ANDROID__)
#include <dlfcn.h>
#endif
#endif

/* Generated per executable by cmake/GenesisBuildInfo.cmake (refreshed every
 * build, rewritten only when it changes, so only this file recompiles). */
#if defined(__has_include)
#if __has_include("genesis_build_info.h")
#include "genesis_build_info.h"
#endif
#endif
#ifndef GENESIS_BUILD_ENGINE_GIT
#define GENESIS_BUILD_ENGINE_GIT "unknown"
#endif
#ifndef GENESIS_BUILD_GAME_GIT
#define GENESIS_BUILD_GAME_GIT "unknown"
#endif
/* A game's own GENESIS_GAME_VERSION define wins; else its CMake project
 * version from the generated header. */
#ifndef GENESIS_GAME_VERSION
#ifdef GENESIS_BUILD_GAME_VERSION
#define GENESIS_GAME_VERSION GENESIS_BUILD_GAME_VERSION
#else
#define GENESIS_GAME_VERSION "dev"
#endif
#endif
#ifdef NDEBUG
#define GENESIS_BUILD_CONFIG "release"
#else
#define GENESIS_BUILD_CONFIG "debug"
#endif

extern const char *exe_relative(const char *);

#define FLUSH_CHECK_FRAMES   600u   /* look at the wall clock this often */
#define FLUSH_MIN_SECONDS    60     /* and rewrite counters at most this often */

typedef struct {
    uint32_t addr;
    char    *note;
} EvEntry;

typedef struct {
    const char *stem;        /* file name without .toml */
    const char *kind;        /* evidence_kind value */
    const char *table;       /* TOML table holding the array */
    const char *key;         /* array key */
    const char *title;       /* first header line */
    const char *guidance;    /* second header line */

    EvEntry *e;
    int      n, cap;
    int     *slot;           /* open addressing; value = entry index + 1 */
    int      slot_cap;
    int      dirty;

    unsigned           sessions;       /* sessions recorded, this one included */
    unsigned long long frames_before;  /* frames emulated by earlier sessions */
    char               first_updated[32];
} EvFile;

static EvFile s_files[RT_EVIDENCE_KIND_COUNT] = {
    { "dispatch_misses", "dispatch_miss", "functions", "extra",
      "Dispatch misses: computed-dispatch targets that had no generated function.",
      "Validate every address against disassembly before adding this file to "
      "game.discovery_files." },
    { "floor_coverage", "tier3_floor", "functions", "extra",
      "Tier-3 floor coverage: in-ROM code the interpreter floor ran (missed entry "
      "plus its call/jump subtree).",
      "Validate every address against disassembly before adding this file to "
      "game.discovery_files." },
    { "interior_label_misses", "interior_label_miss", "interior_labels", "addresses",
      "Interior-label misses: dispatch targets INSIDE an existing function.",
      "Never function seeds (seeding splits the parent); each one is a codegen gap "
      "to fix with in-function dispatch." },
    { "floor_unsafe", "floor_unsafe", "floor_unsafe", "addresses",
      "Floor-declined misses: the interpreter capsule did not return to the native "
      "caller (unbalanced stack or mis-decode).",
      "Root-cause each one; the miss was left as a no-op for that session." },
};

static int      s_inited;
static int      s_fresh;
static char     s_game[64]       = "game";
static char     s_game_name[128] = "";
static char     s_build_id[64];
static char     s_build_info[256];
static unsigned long long s_session_frames;
static unsigned long long s_flushed_frames = ~0ull;
static time_t   s_last_flush;

/* ---- identity --------------------------------------------------------- */

static uint32_t fnv1a32(uint32_t h, const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

static int exe_module_path(char *out, size_t n)
{
#ifdef _WIN32
    HMODULE m = NULL;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)(void *)&genesis_build_exe_fingerprint, &m))
        m = NULL;
    DWORD r = GetModuleFileNameA(m, out, (DWORD)n);
    return r > 0 && r < n;
#elif defined(__APPLE__) || defined(__ANDROID__)
    Dl_info info;
    if (!dladdr((void *)&genesis_build_exe_fingerprint, &info) || !info.dli_fname)
        return 0;
    snprintf(out, n, "%s", info.dli_fname);
    return 1;
#else
    ssize_t r = readlink("/proc/self/exe", out, n - 1);
    if (r <= 0) return 0;
    out[r] = 0;
    return 1;
#endif
}

uint32_t genesis_build_exe_fingerprint(void)
{
    static uint32_t cached;
    if (cached) return cached;
    char path[1024];
    uint32_t h = 2166136261u;
    FILE *f = exe_module_path(path, sizeof path) ? fopen(path, "rb") : NULL;
    if (f) {
        static unsigned char buf[1 << 16];
        size_t got;
        while ((got = fread(buf, 1, sizeof buf, f)) > 0) h = fnv1a32(h, buf, got);
        fclose(f);
    } else {
        fprintf(stderr, "[evidence] cannot read the executable for its build "
                        "fingerprint; using the compile stamp\n");
        h = fnv1a32(h, (const uint8_t *)genesis_build_info_string(),
                    strlen(genesis_build_info_string()));
    }
    cached = h ? h : 1u;
    return cached;
}

const char *genesis_build_info_string(void)
{
    static char s[256];
    if (!s[0])
        snprintf(s, sizeof s, "game %s git %s; engine git %s; compiled %s %s; %s",
                 GENESIS_GAME_VERSION, GENESIS_BUILD_GAME_GIT, GENESIS_BUILD_ENGINE_GIT,
                 __DATE__, __TIME__, GENESIS_BUILD_CONFIG);
    return s;
}

/* ---- small helpers ---------------------------------------------------- */

static void path_of(char *out, size_t n, const char *name)
{
    snprintf(out, n, "%s", exe_relative(name));   /* exe_relative: static buffer */
}

static void now_iso(char *out, size_t n)
{
    time_t t = time(NULL);
    struct tm *g = gmtime(&t);
    if (!g || !strftime(out, n, "%Y-%m-%dT%H:%M:%SZ", g))
        snprintf(out, n, "unknown");
}

static int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

/* Replace dst with src (atomic on both platforms). */
static int replace_file(const char *src, const char *dst)
{
#ifdef _WIN32
    return MoveFileExA(src, dst, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return rename(src, dst) == 0;
#endif
}

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
    return s;
}

static void sanitize(char *s)
{
    for (; *s; s++)
        if (*s == ',' || *s == '[' || *s == ']' || *s == '\n' || *s == '\r' || *s == '#')
            *s = ' ';
}

/* ---- the address set -------------------------------------------------- */

static uint32_t hash_addr(uint32_t a) { a ^= a >> 13; a *= 0x9E3779B1u; return a ^ (a >> 16); }

static int set_find(const EvFile *f, uint32_t addr)
{
    if (!f->slot_cap) return -1;
    for (uint32_t i = hash_addr(addr) & (uint32_t)(f->slot_cap - 1);; i = (i + 1) & (uint32_t)(f->slot_cap - 1)) {
        int v = f->slot[i];
        if (!v) return -1;
        if (f->e[v - 1].addr == addr) return v - 1;
    }
}

static void set_rehash(EvFile *f, int cap)
{
    int *slot = (int *)calloc((size_t)cap, sizeof *slot);
    if (!slot) return;
    free(f->slot);
    f->slot = slot;
    f->slot_cap = cap;
    for (int k = 0; k < f->n; k++) {
        uint32_t i = hash_addr(f->e[k].addr) & (uint32_t)(cap - 1);
        while (f->slot[i]) i = (i + 1) & (uint32_t)(cap - 1);
        f->slot[i] = k + 1;
    }
}

static int set_insert(EvFile *f, uint32_t addr, const char *note)
{
    if (set_find(f, addr) >= 0) return 0;
    if (f->n == f->cap) {
        int cap = f->cap ? f->cap * 2 : 64;
        EvEntry *e = (EvEntry *)realloc(f->e, (size_t)cap * sizeof *e);
        if (!e) return 0;
        f->e = e;
        f->cap = cap;
    }
    if ((f->n + 1) * 2 > f->slot_cap)
        set_rehash(f, f->slot_cap ? f->slot_cap * 2 : 256);
    if ((f->n + 1) * 2 > f->slot_cap) return 0;   /* out of memory */
    char *dup = NULL;
    if (note && note[0]) {
        size_t len = strlen(note);
        dup = (char *)malloc(len + 1);
        if (dup) { memcpy(dup, note, len + 1); sanitize(dup); }
    }
    f->e[f->n].addr = addr;
    f->e[f->n].note = dup;
    f->n++;
    uint32_t i = hash_addr(addr) & (uint32_t)(f->slot_cap - 1);
    while (f->slot[i]) i = (i + 1) & (uint32_t)(f->slot_cap - 1);
    f->slot[i] = f->n;
    return 1;
}

static void set_clear(EvFile *f)
{
    for (int k = 0; k < f->n; k++) free(f->e[k].note);
    f->n = 0;
    if (f->slot) memset(f->slot, 0, (size_t)f->slot_cap * sizeof *f->slot);
}

/* ---- read / write ----------------------------------------------------- */

typedef struct {
    int                exists;
    char               game[64];
    char               build[64];
    unsigned           sessions;
    unsigned long long frames;
    char               first[32];
} EvHeader;

/* Parse header comments and entries of an evidence file into f's set. */
static void read_file(EvFile *f, const char *path, EvHeader *h)
{
    memset(h, 0, sizeof *h);
    set_clear(f);
    FILE *fp = fopen(path, "r");
    if (!fp) return;
    h->exists = 1;
    char line[1024];
    int in_array = 0;
    while (fgets(line, sizeof line, fp)) {
        char *s = trim(line);
        if (s[0] == '#') {
            char *k = trim(s + 1);
            if (!strncmp(k, "game:", 5)) {
                sscanf(k + 5, " %63s", h->game);
            } else if (!strncmp(k, "build_id:", 9)) {
                snprintf(h->build, sizeof h->build, "%s", trim(k + 9));
            } else if (!strncmp(k, "sessions:", 9)) {
                h->sessions = (unsigned)strtoul(k + 9, NULL, 10);
            } else if (!strncmp(k, "frames:", 7)) {
                h->frames = strtoull(k + 7, NULL, 10);
            } else if (!strncmp(k, "first_updated:", 14)) {
                snprintf(h->first, sizeof h->first, "%s", trim(k + 14));
            }
            continue;
        }
        if (!in_array) {
            size_t len = strlen(s);
            if (len && s[len - 1] == '[' && strchr(s, '=')) in_array = 1;
            continue;
        }
        if (s[0] == ']') { in_array = 0; continue; }
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            uint32_t addr = (uint32_t)strtoul(s, NULL, 16) & 0xFFFFFFu;
            char *c = strchr(s, '#');
            set_insert(f, addr, c ? trim(c + 1) : NULL);
        }
    }
    fclose(fp);
}

static int emit_file(FILE *fp, const EvFile *f, const char *when)
{
    fprintf(fp,
            "# %s\n"
            "# %s\n"
            "# Accumulates across launches of this build only; a different build\n"
            "# (executable or ROM) starts a fresh file and keeps the old one as\n"
            "# %s.prev.toml. Each entry comment gives the session and frame it was\n"
            "# first seen in.\n"
            "#\n"
            "# game:           %s (%s)\n"
            "# build_id:       %s\n"
            "# build_info:     %s\n"
            "# sessions:       %u\n"
            "# frames:         %llu\n"
            "# session_frames: %llu (latest session)\n"
            "# entries:        %d\n"
            "# first_updated:  %s\n"
            "# last_updated:   %s\n"
            "format_version = 1\n"
            "evidence_kind = \"%s\"\n\n"
            "[%s]\n"
            "%s = [\n",
            f->title, f->guidance, f->stem,
            s_game, s_game_name, s_build_id, s_build_info,
            f->sessions, f->frames_before + s_session_frames, s_session_frames,
            f->n, f->first_updated, when,
            f->kind, f->table, f->key);
    /* Every element keeps its comma so a trailing comment never touches the
     * value: line-oriented readers split on ',' and drop '#' pieces. */
    for (int k = 0; k < f->n; k++) {
        if (f->e[k].note)
            fprintf(fp, "  0x%06X, # %s\n", f->e[k].addr, f->e[k].note);
        else
            fprintf(fp, "  0x%06X,\n", f->e[k].addr);
    }
    fprintf(fp, "]\n");
    int ok = fflush(fp) == 0;
#ifdef _WIN32
    if (ok) _commit(_fileno(fp));
#else
    if (ok) fsync(fileno(fp));
#endif
    return (fclose(fp) == 0) && ok;
}

/* Crash-safe rewrite: temp file + atomic replace. */
static int write_file(const EvFile *f)
{
    char path[600], tmp[640], rel[200], when[32];
    snprintf(rel, sizeof rel, "%s.toml", f->stem);
    path_of(path, sizeof path, rel);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    now_iso(when, sizeof when);

    FILE *fp = fopen(tmp, "w");
    if (fp) {
        if (emit_file(fp, f, when) && replace_file(tmp, path)) return 1;
        remove(tmp);
    }
    /* No temp file, or the destination is held open (Windows sharing): keep
     * the evidence by writing in place rather than dropping it. */
    fp = fopen(path, "w");
    return fp ? emit_file(fp, f, when) : 0;
}

static void game_variant_name(char *out, size_t n, const char *stem, const char *game)
{
    char rel[200];
    snprintf(rel, sizeof rel, "%s.%s.toml", stem, game);
    path_of(out, n, rel);
}

static void load_one(EvFile *f)
{
    char path[600], prev[600], mine[600], rel[200];
    EvHeader h;
    snprintf(rel, sizeof rel, "%s.toml", f->stem);
    path_of(path, sizeof path, rel);
    snprintf(rel, sizeof rel, "%s.prev.toml", f->stem);
    path_of(prev, sizeof prev, rel);
    game_variant_name(mine, sizeof mine, f->stem, s_game);

    read_file(f, path, &h);
    if (h.exists && h.game[0] && strcmp(h.game, s_game) != 0) {
        /* Another game executable shares this directory: park its file under
         * its own name and bring ours back if we parked it earlier. */
        char theirs[600];
        game_variant_name(theirs, sizeof theirs, f->stem, h.game);
        if (replace_file(path, theirs))
            fprintf(stderr, "[evidence] %s.toml belongs to %s; parked it as %s.%s.toml\n",
                    f->stem, h.game, f->stem, h.game);
        memset(&h, 0, sizeof h);   /* none of its counters are ours */
        set_clear(f);
    }
    if (!h.exists && file_exists(mine) && replace_file(mine, path))
        read_file(f, path, &h);

    if (h.exists) {
        const char *why = s_fresh ? "fresh evidence requested"
                        : !h.build[0] ? "no build stamp (older runner)"
                        : strcmp(h.build, s_build_id) ? "written by a different build"
                        : NULL;
        if (why) {
            if (f->n > 0 && replace_file(path, prev))
                fprintf(stderr, "[evidence] %s.toml: %s (%s); moved %d entries to %s.prev.toml\n",
                        f->stem, why, h.build[0] ? h.build : "unstamped", f->n, f->stem);
            set_clear(f);
            memset(&h, 0, sizeof h);
        }
    }
    f->sessions      = h.sessions + 1;
    f->frames_before = h.frames;
    if (h.first[0]) snprintf(f->first_updated, sizeof f->first_updated, "%s", h.first);
    else            now_iso(f->first_updated, sizeof f->first_updated);
    f->dirty = 0;
    write_file(f);
    if (f->n)
        fprintf(stderr, "[evidence] %s.toml: %d entries from %u earlier session(s) of this build\n",
                f->stem, f->n, h.sessions);
}

/* ---- public ----------------------------------------------------------- */

void runtime_evidence_request_fresh(void) { s_fresh = 1; }

static void evidence_atexit(void) { runtime_evidence_flush(); }

void runtime_evidence_init(const char *game_short_name, const char *game_display_name,
                           const uint8_t *rom, size_t rom_len)
{
    if (s_inited) return;
    s_inited = 1;
    {
        const char *v = getenv("GENESIS_EVIDENCE_FRESH");
        if (v && v[0] && v[0] != '0') s_fresh = 1;
    }
    /* The game tag becomes part of parked file names: keep it filename-safe. */
    if (game_short_name && game_short_name[0]) {
        size_t j = 0;
        for (const char *c = game_short_name; *c && j + 1 < sizeof s_game; c++)
            if ((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') ||
                (*c >= '0' && *c <= '9') || *c == '_' || *c == '-')
                s_game[j++] = *c;
        s_game[j] = 0;
        if (!j) snprintf(s_game, sizeof s_game, "game");
    }
    snprintf(s_game_name, sizeof s_game_name, "%s",
             game_display_name && game_display_name[0] ? game_display_name : s_game);
    sanitize(s_game_name);
    snprintf(s_build_id, sizeof s_build_id, "exe=%08x rom=%08x",
             (unsigned)genesis_build_exe_fingerprint(),
             (unsigned)fnv1a32(2166136261u, rom, rom ? rom_len : 0));
    snprintf(s_build_info, sizeof s_build_info, "%s", genesis_build_info_string());
    sanitize(s_build_info);

    for (int k = 0; k < RT_EVIDENCE_KIND_COUNT; k++) load_one(&s_files[k]);
    s_last_flush = time(NULL);
    s_flushed_frames = 0;
    atexit(evidence_atexit);
    unsigned session = 0;
    for (int k = 0; k < RT_EVIDENCE_KIND_COUNT; k++)
        if (s_files[k].sessions > session) session = s_files[k].sessions;
    fprintf(stderr, "[evidence] build %s (%s); session %u of this build\n",
            s_build_id, s_build_info, session);
}

int runtime_evidence_has(RuntimeEvidenceKind kind, uint32_t addr)
{
    return (unsigned)kind < RT_EVIDENCE_KIND_COUNT &&
           set_find(&s_files[kind], addr & 0xFFFFFFu) >= 0;
}

int runtime_evidence_add(RuntimeEvidenceKind kind, uint32_t addr, uint64_t frame,
                         const char *note)
{
    if ((unsigned)kind >= RT_EVIDENCE_KIND_COUNT) return 0;
    EvFile *f = &s_files[kind];
    addr &= 0xFFFFFFu;
    if (set_find(f, addr) >= 0) return 0;
    char full[256];
    snprintf(full, sizeof full, "session %u frame %llu%s%s", f->sessions,
             (unsigned long long)frame, note && note[0] ? " " : "", note ? note : "");
    if (!set_insert(f, addr, full)) return 0;
    f->dirty = 1;
    return 1;
}

void runtime_evidence_sync(RuntimeEvidenceKind kind)
{
    if ((unsigned)kind >= RT_EVIDENCE_KIND_COUNT || !s_inited) return;
    EvFile *f = &s_files[kind];
    if (!f->dirty) return;
    f->dirty = 0;
    write_file(f);
}

int runtime_evidence_count(RuntimeEvidenceKind kind)
{
    return (unsigned)kind < RT_EVIDENCE_KIND_COUNT ? s_files[kind].n : 0;
}

void runtime_evidence_tick(void)
{
    s_session_frames++;
    if (!s_inited || s_session_frames % FLUSH_CHECK_FRAMES) return;
    time_t now = time(NULL);
    if (now - s_last_flush >= FLUSH_MIN_SECONDS) runtime_evidence_flush();
}

void runtime_evidence_flush(void)
{
    if (!s_inited) return;
    int any_dirty = 0;
    for (int k = 0; k < RT_EVIDENCE_KIND_COUNT; k++) any_dirty |= s_files[k].dirty;
    s_last_flush = time(NULL);
    if (!any_dirty && s_flushed_frames == s_session_frames) return;
    s_flushed_frames = s_session_frames;
    for (int k = 0; k < RT_EVIDENCE_KIND_COUNT; k++) {
        s_files[k].dirty = 0;
        write_file(&s_files[k]);
    }
}
