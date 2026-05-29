/* sonic3k_hybrid_table.c — hybrid table for the oracle build's sandbox-compare.
 *
 * The oracle runs the clown68000 interpreter as ground truth. For every PC
 * listed here, runner/hybrid.c runs the recompiled native function in a
 * sandbox and diffs its register + work-RAM output against the interpreter,
 * logging [DIV] to stderr (oracle_run.log). See hybrid.c for the mechanism.
 *
 * ── Investigation: title-screen animation garble (recompiler bug) ──────────
 * The static title art renders clean; the *persistent* animated elements
 * garble — Sonic's hand/eye, a bouncing object, a floating bg object. Those
 * are the looping title sprite OBJECTS (finger / wink / Tails-plane), driven
 * each frame from Process_Sprites ($21919E). The one-shot Sonic-wave intro
 * (Iterate_TitleSonicFrame → TitleSonic_LoadFrame) finishes by ~frame 100,
 * before the sandbox's frame>=1000 guard, so it is not the persistent garble.
 *
 * ROUND 1 (per-object localization): we list each title object's dispatch
 * ENTRY as an outermost match and deliberately OMIT Process_Sprites, so the
 * object routines are NOT masked by an enclosing in-table function (hybrid.c
 * skips nested matches while a comparison is pending). Each object is thus
 * tested individually in one run:
 *   - exactly one object diverges  → bug in that object's own codegen
 *   - many objects diverge         → bug in a shared leaf they all call
 *                                     (Animate_Sprite / DPLC), also listed
 *                                     below so it can be exonerated/implicated.
 */
#include "hybrid.h"

/* Title sprite objects (dispatch entry PCs, S3 half @ +$200000) */
extern void func_203F08(void);   /* Obj_TitleBanner      */
extern void func_203FF4(void);   /* Obj_TitleTM          */
extern void func_20407A(void);   /* Obj_TitleCopyright   */
extern void func_2040B8(void);   /* Obj_TitleSelection   */
extern void func_204140(void);   /* Obj_TitleSonicFinger — Sonic's hand   */
extern void func_2041A4(void);   /* Obj_TitleSonicWink   — Sonic's eye     */
extern void func_2041F6(void);   /* Obj_TitleTailsPlane  — floating bg obj */

/* Shared animation leaves (tested only when reached as outermost, i.e. not
 * via one of the objects above — but if a shared leaf is the culprit, every
 * object that calls it will already diverge in its own row). */
extern void func_219256(void);   /* Animate_Sprite                */
extern void func_2192F6(void);   /* Animate_SpriteIrregularDelay  */

/* Per-frame title helpers that keep running at the title (siblings of the
 * objects under Wait_Title; not masked by anything). */
extern void func_2039F4(void);   /* TitleAnim_FlipBuffer          */
extern void func_203A76(void);   /* Iterate_TitleSonicFrame       */

HybridEntry g_hybrid_table[] = {
    { 0x203F08u, func_203F08 },
    { 0x203FF4u, func_203FF4 },
    { 0x20407Au, func_20407A },
    { 0x2040B8u, func_2040B8 },
    { 0x204140u, func_204140 },
    { 0x2041A4u, func_2041A4 },
    { 0x2041F6u, func_2041F6 },
    { 0x219256u, func_219256 },
    { 0x2192F6u, func_2192F6 },
    { 0x2039F4u, func_2039F4 },
    { 0x203A76u, func_203A76 },
};

int g_hybrid_table_size = (int)(sizeof(g_hybrid_table) / sizeof(g_hybrid_table[0]));
