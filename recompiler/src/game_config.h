/*
 * game_config.h — game.toml parser interface.
 *
 * The legacy whitespace `.cfg` format has been retired; see
 * game_config.c for the TOML schema and how `discovery_files`
 * recursively merge auto-generated address tables into this struct.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

/* Encoding of each entry in a static jump table.
 *
 * JT_FMT_ABS_L   : 32-bit absolute target per entry (stride defaults 4).
 * JT_FMT_PCREL_W : 16-bit signed offset added to base_addr (stride 2);
 *                  this is the common Sega pattern emitted by
 *                  `JMP <table>(PC,Dn.W)` where the assembler assembles
 *                  `dc.w  (target - <table>)` rows.
 * JT_FMT_BRA_W   : 4-byte bra.w trampolines per entry (stride 4). The
 *                  JMP lands ON the trampoline body itself; each entry
 *                  IS a function entry at base + i*stride.
 * JT_FMT_BRA_S   : 2-byte bra.s trampolines per entry (stride 2). Same
 *                  semantics as BRA_W, narrower instruction.
 */
typedef enum {
    JT_FMT_ABS_L  = 0,
    JT_FMT_PCREL_W = 1,
    JT_FMT_BRA_W  = 2,
    JT_FMT_BRA_S  = 3,
} JumpTableFormat;

typedef struct {
    uint32_t        start_addr;     /* table base                          */
    uint32_t        end_addr;       /* exclusive end (start + stride*N)    */
    uint32_t        stride_bytes;   /* 2 or 4 typically                    */
    JumpTableFormat format;
} JumpTableEntry;

typedef struct { uint32_t lo; uint32_t hi; } ProtectedRange;

/*
 * Widescreen (16:9) injection site — a single instruction in the ORIGINAL
 * (unmodified) ROM whose generated C the recompiler widens by a runtime margin
 * (`g_ws_margin`, extra pixels per side; 0 => byte-identical 4:3). This is the
 * post-patch widening layer: the disasm is NEVER edited, the ROM is recompiled
 * as-is, and the widening lives entirely in the emitted C. Mirrors the
 * snesrecomp/psxrecomp model (recompile original binary, widen on top).
 *
 *   WS_SITE_MASK10 : widen an `andi #imm` immediate one bit (e.g. $1FF -> $3FF)
 *                    so a 9-bit sprite-X clips past 512 instead of wrapping.
 *   WS_SITE_ADDREG : emit  D[reg] += (g_ws_margin >> shift)  BEFORE the insn
 *                    (right/upper bound; row-block count uses shift=3 = /8).
 *   WS_SITE_SUBREG : emit  D[reg] -= (g_ws_margin >> shift)  BEFORE the insn
 *                    (left/lower bound, tile-load start, leading-edge column).
 *   WS_SITE_CULL_LEFT : at a `bmi` site, REPLACE the flag-based condition with
 *                    (int16_t)D[reg] < -(int16_t)(g_ws_margin>>shift) so a left-edge
 *                    cull triggers margin px further out. D[reg] still holds the value
 *                    the bmi's flags reflect (set by the preceding add/sub), so at
 *                    margin 0 it's byte-identical. NON-MUTATING (unlike add/subreg) —
 *                    a right-edge cmpi sharing the same register still sees the
 *                    unmodified value. Only valid on a `bmi` (Bcc cond MI); otherwise
 *                    ignored with a diagnostic.
 *   WS_SITE_ADDIMM / WS_SITE_SUBIMM : widen the IMMEDIATE that a `moveq #imm,Dn` or
 *                    `move.w #imm,Dn` writes, by +/-(g_ws_margin>>shift), at the
 *                    instruction's own address. Used for tile-load column seeds
 *                    (moveq #-16,d5 -> subimm; move.w #320,d5 -> addimm) and the
 *                    full-screen row-block count (moveq #21,d6 -> addimm shift=3).
 *                    In-place (no next-instruction guesswork) and only touches the
 *                    one entry, so it won't over-widen a shared callee. reg is the
 *                    destination Dn. margin 0 => identical.
 *   WS_SITE_CALL_WIDEN : at a `bsr/jsr` site, preset  D[reg] = base + (g_ws_margin>>shift)
 *                    and RETARGET the call to `target` instead of the original callee.
 *                    For row-count widening where the real callee resets the count
 *                    (e.g. S2 DrawBlockRow `moveq #21,d6` @ DF92): retarget the chosen
 *                    callers to a sibling entry (DrawBlockRow_CustomWidth @ DF8A) that
 *                    skips the reset and uses the preset count — so ONLY those callers
 *                    widen, not every caller of the shared callee. `base` = the count
 *                    the callee would have set (e.g. 21). margin 0 => target still gets
 *                    base, identical. The original ret-address push/pop is unchanged.
 *   WS_SITE_CULL_WINDOW_LEFT : at a `bhi` (unsigned-higher) left-window clamp, MUTATE
 *                    D[reg] -= (g_ws_margin>>shift) (word, extends the left edge) and
 *                    REPLACE the branch with a SIGNED `(int16_t)D[reg] > 0` test. The
 *                    mutate destroys the borrow the original `bhi` relied on, so the
 *                    signed `bgt` is the correct equivalent (matches the disasm's
 *                    bhi->bgt). margin 0 => D[reg] unchanged and bgt == bhi here
 *                    (the clamp path is taken for D[reg] <= 0 either way). Only valid
 *                    on a `bhi` (Bcc cond HI); otherwise ignored with a diagnostic.
 *                    Used for the RingsManager visible-window left edge.
 * `scale` (default 1) multiplies the margin for addreg/subreg — e.g. scale=2 adds
 * 2*(margin>>shift), used where a single site must widen by both margins (the ring
 * window's right edge, which also undoes the left edge's -margin).
 * reg indexes a data register (0..7); A-registers are out of scope (the
 * widened bounds are all computed in D-registers in these games).
 */
typedef enum {
    WS_SITE_MASK10 = 0,
    WS_SITE_ADDREG = 1,
    WS_SITE_SUBREG = 2,
    WS_SITE_CULL_LEFT = 3,
    WS_SITE_ADDIMM = 4,
    WS_SITE_SUBIMM = 5,
    WS_SITE_CALL_WIDEN = 6,
    WS_SITE_CULL_WINDOW_LEFT = 7,
} WsSiteKind;

typedef struct {
    uint32_t   addr;    /* canonical-ROM instruction address                */
    WsSiteKind kind;
    uint8_t    reg;     /* D-register index (addreg/subreg/cull_left/imm/call_widen/cull_window_left) */
    uint8_t    shift;   /* margin >> shift: 0 = pixels, 3 = 16px blocks       */
    uint8_t    scale;   /* addreg/subreg margin multiplier (default 1; e.g. 2)*/
    uint16_t   base;    /* call_widen: count the callee would set (e.g. 21)   */
    uint32_t   target;  /* call_widen: retarget call address (e.g. 0xDF8A)    */
} WsSite;

/*
 * Per-game RAM layout, parsed from the [ram_layout] table of game.toml.
 * The recompiler reads these here, then emits <prefix>_layout.c that
 * declares the runtime struct (GameRamLayout in runner/game_layout.h)
 * and instantiates `g_game_layout` with the same values.
 *
 * `present` is true when [ram_layout] was found in the source TOML.
 * The recompiler refuses to emit a layout TU for a config without it
 * — partial migration is not allowed; once shared runner code reads
 * g_game_layout, every game must populate the table.
 */
#define GAMECFG_LEVEL_MODES_MAX 16

typedef struct {
    bool     present;
    uint32_t game_mode_addr;
    uint32_t vint_runcount_addr;
    uint32_t vint_routine_addr;
    uint32_t plc_pending_addr;     /* 0 = no PLC system */
    uint32_t initial_ssp;
    uint32_t vbla_stack;
    uint32_t intr_stack;
    uint32_t player_object_addr;
    uint8_t  level_modes[GAMECFG_LEVEL_MODES_MAX];
    int      level_mode_count;

    /* ---- Widescreen (16:9) opt-in, from the [widescreen] table ----
     * Absent table => ws_capable=false => engine never widens this game
     * (authentic 4:3). See runner/game_layout.h for field semantics. */
    bool     ws_capable;
    uint32_t ws_extra_ram_addr;     /* free-RAM word: engine writes, recompiled 68K reads (0 = none) */
    int      ws_max_extra_cells;    /* per-side cap in 8px cells (clamped further by output buffer) */
    uint8_t  ws_eligible_modes[GAMECFG_LEVEL_MODES_MAX]; /* modes that render wide (EXACT match); empty => use level_modes */
    int      ws_eligible_mode_count;
    uint32_t ws_level_started_addr; /* byte: require != 0 to widen (0 = gate disabled) */
    uint32_t ws_two_player_addr;    /* word: require == 0 to widen (0 = gate disabled) */
    uint32_t ws_redraw_flag_addr;   /* byte: engine writes 1 when widescreen turns on, to
                                     * force a full tile redraw filling the margins (0 = off) */
} GameRamLayoutCfg;

/*
 * GameConfig — every list grows on demand. After game_config_load,
 * the arrays are owned by the cfg; call game_config_free to release
 * them (or just leak them at process exit, which is what the CLI
 * does today). Counts are how many entries are populated; capacities
 * are the current allocation size and are an internal detail.
 */
typedef struct {
    char           output_prefix[64];
    char           annotations_path[256];
    char           symbols_path[256];       /* TOML symbols file (replaces extra_func) */
    JumpTableEntry *jump_tables;
    int            jump_table_count;
    int            jump_table_cap;
    uint32_t       *extra_funcs;
    int            extra_func_count;
    int            extra_func_cap;
    /* Audited function entries registered only after heuristic discovery has
     * reached its fixed point. Their ordinary direct-call/branch closure is
     * walked, but speculative table detectors are disabled during that second
     * pass. This prevents a correct late entry in an oracle-less game from
     * exposing unrelated data-shaped tables and triggering an over-discovery
     * cascade. */
    uint32_t       *late_extra_funcs;
    int            late_extra_func_count;
    int            late_extra_func_cap;
    /* Additional INTERIOR PCs to seed scan_function's CFG-walk worklist.
     * Sourced from disasm local labels (asm68k `.foo` scoped under a
     * parent global label). NOT promoted to function entries — they're
     * just hints so the CFG walker discovers PCs that are reached only
     * by `JMP (PC,Dn.W)` (e.g. the Sonic 2 CPZ Duff's-device pattern). */
    uint32_t       *extra_seeds;
    int            extra_seed_count;
    int            extra_seed_cap;
    uint32_t       *blacklist;
    int            blacklist_count;
    int            blacklist_cap;
    /* Disasm "is this address code?" oracle (instruction-start set), loaded
     * from game.toml `code_addrs_file` (one hex addr per line, e.g. produced
     * by tests/tools/gen_code_addrs.py). When non-empty, boundary-split /
     * dispatch-seed promotion is gated on membership: an extern target that
     * lands on a known DATA address is never promoted to a function entry,
     * killing the data-as-code false-positive class. Empty => no gating
     * (default; preserves prior behavior for games without the file).
     * Kept SORTED so game_config_is_known_code can binary-search. */
    uint32_t       *code_addrs;
    int            code_addr_count;
    int            code_addr_cap;
    /* Runtime executed-PC oracle (instruction-start set actually executed in a
     * trusted emulator run), loaded from game.toml `runtime_exec_file` (one hex
     * addr per line, produced by rka/gen_runtime_oracle.py). Used as an ADDITIVE
     * promotion gate for speculative jump-table targets: a target beyond what
     * static discovery already accepts is promoted only if it was observed
     * executing. Empty => no runtime gating (default; existing behavior). Kept
     * SORTED for binary search. */
    uint32_t       *runtime_exec;
    int            runtime_exec_count;
    int            runtime_exec_cap;
    /* Opt-in for speculative table walkers to promote runtime-observed
     * targets. Merely loading runtime_exec_file leaves phase-0 discovery
     * unchanged, allowing other consumers to use the oracle as an evidence
     * intersection without perturbing the baseline function set. */
    bool           runtime_table_promotions;
    uint32_t       vblank_yield_addr;   /* 0 = not set; emit glue_yield_for_vblank() for this function */
    ProtectedRange *protected_ranges;
    int            protected_range_count;
    int            protected_range_cap;
    /* When true, the validator tolerates Bcc/BSR/BRA forms that use
     * a 32-bit displacement (d8 == 0xFF). Those are 68020+ extensions
     * and out of scope for MC68000-only Genesis ROMs unless a game
     * specifically opts in. Default: false. */
    bool           allow_68020_branch;
    /* When true, function_finder auto-walks PC-indexed JMP tables
     * with no matching jump_table directive in this config. The
     * walk is conservative (validator gate per entry, max 256
     * entries) but can still mis-identify random ROM data as code
     * for some games — keep off unless you've audited the ROM.
     * Manual jump_table directives are always honored regardless of
     * this flag. Default: false. */
    bool           jump_table_autodiscovery;
    /* Widescreen (16:9) injection sites, from [[widescreen_site]] in game.toml.
     * Consumed at codegen time (not emitted to the runtime layout). Empty =>
     * no widescreen injection for this game. */
    WsSite         *ws_sites;
    int            ws_site_count;
    int            ws_site_cap;
    GameRamLayoutCfg ram_layout;
} GameConfig;

/* Returns the widescreen site for `addr`, or NULL if none. */
const WsSite *game_config_ws_site(const GameConfig *cfg, uint32_t addr);

/* Returns true if addr falls in a protected range (no boundary splitting) */
bool game_config_is_protected(const GameConfig *cfg, uint32_t addr);

/* Returns true if addr is in the blacklist */
bool game_config_is_blacklisted(const GameConfig *cfg, uint32_t addr);

/* Returns true if addr is a known instruction-start per the disasm code-addr
 * oracle. If no code_addrs_file was loaded (code_addr_count == 0) this returns
 * true for every address (gating disabled — prior behavior). */
bool game_config_is_known_code(const GameConfig *cfg, uint32_t addr);

/* True if a runtime executed-PC oracle is loaded for this game. */
bool game_config_has_runtime_oracle(const GameConfig *cfg);

/* True only when an oracle is loaded AND runtime_table_promotions is enabled. */
bool game_config_runtime_table_promotions(const GameConfig *cfg);

/* True if addr was observed executing in the runtime oracle. Returns false if
 * no oracle is loaded (so callers must guard additive promotion on
 * game_config_has_runtime_oracle first). */
bool game_config_runtime_observed(const GameConfig *cfg, uint32_t addr);

void game_config_init_empty(GameConfig *cfg);
void game_config_free(GameConfig *cfg);
bool game_config_load(GameConfig *cfg, const char *path);

/* Emit <prefix>_layout.c — defines `const GameRamLayout g_game_layout`
 * populated from cfg->ram_layout. Returns false if [ram_layout] was
 * absent in the source TOML; the caller decides whether to treat that
 * as a build failure. The output_path argument is the full file path
 * (typically generated/<prefix>_layout.c). */
bool game_config_emit_layout(const GameConfig *cfg, const char *output_path);
