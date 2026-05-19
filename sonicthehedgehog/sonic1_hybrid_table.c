/*
 * sonic1_hybrid_table.c — Sonic 1 dispatch table for the oracle build's
 * hybrid interpreter system. Renamed from runner/hybrid_table.c when
 * the table moved out of the shared runner: each game owns its own
 * verified-clean func_NNNNNN list (Sonic 2's lives in sonic2_hybrid_table.c).
 *
 * Phase 1 verification: 38 functions.
 * Removed: func_0011E6 (IO ports), func_005EBE (wild divergence),
 *          func_0015B2 (calls func_0015DE not in table)
 */

#include "hybrid.h"

/* === Batches 1-3: 26 verified clean === */
extern void func_014C4C(void);  extern void func_001396(void);
extern void func_00139C(void);  extern void func_0729B6(void);
extern void func_0040F0(void);  extern void func_004118(void);
extern void func_003572(void);  extern void func_0137A0(void);
extern void func_07260C(void);  extern void func_0029DA(void);
extern void func_0020FC(void);  extern void func_00188C(void);
extern void func_003586(void);  extern void func_004206(void);
extern void func_005370(void);  extern void func_01704C(void);
extern void func_00416E(void);  extern void func_006D32(void);
extern void func_01C8DA(void);  extern void func_01C8D0(void);
extern void func_01C9A8(void);  extern void func_01C9B2(void);
extern void func_006616(void);  extern void func_01B098(void);
extern void func_003460(void);  extern void func_0034E6(void);

/* === Batch 4 === */
extern void func_001452(void);  /* Inputs */
extern void func_0061EC(void);  /* BgScrollSpeed — uses JMP table, now with interpreter fallback */
extern void func_003FD0(void);  /* MoveSonicInDemo */
extern void func_003BD0(void);  /* LZWaterFeatures */
extern void func_003E32(void);  /* LZWindTunnels */
extern void func_003F34(void);  /* LZWaterSlides */
extern void func_01C89A(void);  /* HudDb_XY2 */
extern void func_005898(void);  /* EndingDemoLoad */
extern void func_01C938(void);  /* ContScrCounter */
extern void func_006CA6(void);  /* LevelDataLoad */

extern void func_071F02(void);  extern void func_001E26(void);
extern void func_001EA0(void);  extern void func_001F4C(void);
extern void func_001FCA(void);  extern void func_01B290(void);
extern void func_00D8DA(void);  extern void func_00193C(void);

extern void func_07267C(void);  extern void func_07272E(void);
extern void func_07296A(void);  extern void func_072722(void);
extern void func_0065B2(void);  extern void func_01C87A(void);
extern void func_0029A8(void);  extern void func_000C36(void);
extern void func_0015DE(void);  extern void func_000B64(void);
extern void func_072764(void);  extern void func_002118(void);
extern void func_002130(void);  extern void func_00214C(void);
extern void func_071D40(void);  extern void func_0005CA(void);
extern void func_00D604(void);  extern void func_0726FE(void);
extern void func_00195C(void);  extern void func_001580(void);
extern void func_006C20(void);  extern void func_071D60(void);
extern void func_071DC6(void);  extern void func_071D22(void);
extern void func_00D762(void);  extern void func_01C510(void);
extern void func_006BD6(void);

/* === Batch A === */
extern void func_0005E0(void);  extern void func_072A5A(void);
extern void func_0729A0(void);  extern void func_0005BA(void);
extern void func_01C024(void);  extern void func_001E72(void);
extern void func_001F9C(void);  extern void func_0015B2(void);
extern void func_00657E(void);  extern void func_001DE4(void);
extern void func_001F0A(void);  extern void func_071E18(void);
extern void func_01C80E(void);  extern void func_07256A(void);
extern void func_0728E2(void);  extern void func_0728DC(void);
extern void func_001222(void);  extern void func_004274(void);
extern void func_072CB4(void);  extern void func_0012C4(void);
extern void func_006D12(void);  extern void func_0728AC(void);
extern void func_072926(void);  extern void func_00106E(void);
extern void func_00171E(void);  extern void func_000FA6(void);
extern void func_00152E(void);  extern void func_01CA6E(void);
extern void func_01CA0C(void);  extern void func_006B32(void);
extern void func_00200A(void);  extern void func_004962(void);
extern void func_004BE4(void);  extern void func_00189C(void);
extern void func_005EBE(void);  extern void func_00D750(void);
extern void func_001DE4(void);  extern void func_00657E(void);

extern void func_072504(void);
extern void func_071CCA(void);
extern void func_072850(void);
extern void func_0069F4(void);
extern void func_006954(void);
extern void func_006C58(void);
extern void func_0068B2(void);
extern void func_006886(void);
extern void func_071D9E(void);
extern void func_001352(void);
extern void func_006B06(void);
extern void func_006ADA(void);
extern void func_006AD8(void);
extern void func_006B04(void);
extern void func_01C822(void);
extern void func_0725CA(void);
extern void func_0015EC(void);
extern void func_071CEC(void);
extern void func_072878(void);
extern void func_071C4E(void);
extern void func_01B67C(void);
extern void func_01C6B8(void);
extern void func_001E52(void);
extern void func_001F7C(void);
extern void func_001DB2(void);
extern void func_001DAC(void);
extern void func_001ED0(void);
extern void func_006C7E(void);
extern void func_0068B2(void);  extern void func_072878(void);
extern void func_071CCA(void);  extern void func_072850(void);

/* Stack-level caller functions */
extern void func_012E18(void);  /* Obj01_MdNormal */
extern void func_012C3E(void);  /* Sonic_Control */
extern void func_012D78(void);  /* Sonic_Water */
extern void func_0133EA(void);  /* Sonic_Jump */
extern void func_012EB0(void);  /* Sonic_Move */
extern void func_0134D4(void);  /* Sonic_SlopeResist */
HybridEntry g_hybrid_table[] = {
#if 0  /* Leaf-level tests disabled for stack-level focus */
    /* Batches 1-3 (26 verified) */
    { 0x014C4Cu, func_014C4C }, { 0x001396u, func_001396 },
    { 0x00139Cu, func_00139C }, { 0x0729B6u, func_0729B6 },
    { 0x0040F0u, func_0040F0 }, { 0x004118u, func_004118 },
    { 0x003572u, func_003572 }, { 0x0137A0u, func_0137A0 },
    { 0x07260Cu, func_07260C }, { 0x0029DAu, func_0029DA },
    { 0x0020FCu, func_0020FC }, { 0x00188Cu, func_00188C },
    { 0x003586u, func_003586 }, { 0x004206u, func_004206 },
    { 0x005370u, func_005370 }, { 0x01704Cu, func_01704C },
    { 0x00416Eu, func_00416E }, { 0x006D32u, func_006D32 },
    { 0x01C8DAu, func_01C8DA }, { 0x01C8D0u, func_01C8D0 },
    { 0x01C9A8u, func_01C9A8 }, { 0x01C9B2u, func_01C9B2 },
    { 0x006616u, func_006616 }, { 0x01B098u, func_01B098 },
    { 0x003460u, func_003460 }, { 0x0034E6u, func_0034E6 },
    /* Batch 4 */
    { 0x001452u, func_001452 },
    { 0x0061ECu, func_0061EC },
    { 0x003FD0u, func_003FD0 },
    { 0x003BD0u, func_003BD0 },
    { 0x003E32u, func_003E32 },
    { 0x003F34u, func_003F34 },
    { 0x01C89Au, func_01C89A },
    { 0x005898u, func_005898 },
    { 0x01C938u, func_01C938 },
    /* Batch 5 test */
    { 0x071F02u, func_071F02 },
    { 0x001E26u, func_001E26 },
    { 0x001EA0u, func_001EA0 },
    { 0x001F4Cu, func_001F4C },
    { 0x001FCAu, func_001FCA },
    { 0x01B290u, func_01B290 },
    { 0x00D8DAu, func_00D8DA },
    { 0x00193Cu, func_00193C },
    /* Batch 6 only */
    { 0x07267Cu, func_07267C },
    { 0x07272Eu, func_07272E },
    { 0x07296Au, func_07296A },
    { 0x072722u, func_072722 },
    { 0x0065B2u, func_0065B2 },
    { 0x01C87Au, func_01C87A },
    /* Batch 7 first half */
    { 0x000C36u, func_000C36 },
    { 0x0015DEu, func_0015DE },
    /* SKIP func_000B64 — RTE handler, not a normal function */
    { 0x072764u, func_072764 },
    { 0x002118u, func_002118 },
    { 0x002130u, func_002130 },
    { 0x00214Cu, func_00214C },
    { 0x071D40u, func_071D40 },
    { 0x0005CAu, func_0005CA },
    { 0x00D604u, func_00D604 },
    /* Batch 7 second half */
    { 0x0726FEu, func_0726FE },
    { 0x00195Cu, func_00195C },
    { 0x001580u, func_001580 },
    { 0x006C20u, func_006C20 },
    { 0x071D60u, func_071D60 },
    /* SKIP func_071DC6 — shadow timeout: call chain hits VBlank wait at $0029AC */
    { 0x071D22u, func_071D22 },
    { 0x00D762u, func_00D762 },
    { 0x01C510u, func_01C510 },
    { 0x006BD6u, func_006BD6 },
    /* Batch A: only the ones that don't diverge */
    { 0x0005E0u, func_0005E0 },
    { 0x0729A0u, func_0729A0 },
    { 0x0005BAu, func_0005BA },
    { 0x001F9Cu, func_001F9C },
    { 0x001F0Au, func_001F0A },
    { 0x01C80Eu, func_01C80E },
    { 0x0728E2u, func_0728E2 },
    { 0x0728DCu, func_0728DC },
    { 0x001222u, func_001222 },
    { 0x004274u, func_004274 },
    /* Clean from remaining batch */
    { 0x0012C4u, func_0012C4 },
    { 0x072926u, func_072926 },
    { 0x000FA6u, func_000FA6 },
    { 0x01CA6Eu, func_01CA6E },
    { 0x01CA0Cu, func_01CA0C },
    { 0x006B32u, func_006B32 },
    { 0x00200Au, func_00200A },
    { 0x004962u, func_004962 },
    { 0x004BE4u, func_004BE4 },
    { 0x00D750u, func_00D750 },
    /* JSR stack fix unlocked these */
    { 0x001E72u, func_001E72 },
    { 0x001DE4u, func_001DE4 },
    { 0x00657Eu, func_00657E },
    { 0x006D12u, func_006D12 },
    { 0x072CB4u, func_072CB4 },
    { 0x0728ACu, func_0728AC },
    { 0x0015B2u, func_0015B2 },
    /* SKIP func_00171E — JMP table handlers hit VBlank wait, interpreter timeout corrupts state */
    { 0x072504u, func_072504 },
    { 0x071E18u, func_071E18 },
    { 0x07256Au, func_07256A },
    { 0x01C80Eu, func_01C80E },
    { 0x0005E0u, func_0005E0 },
    { 0x0005BAu, func_0005BA },
    { 0x01C024u, func_01C024 },
    { 0x001F9Cu, func_001F9C },
    { 0x001F0Au, func_001F0A },
    { 0x0069F4u, func_0069F4 },
    { 0x006954u, func_006954 },
    { 0x006C58u, func_006C58 },
    { 0x006886u, func_006886 },
    { 0x006B06u, func_006B06 },
    { 0x006ADAu, func_006ADA },
    { 0x006AD8u, func_006AD8 },
    { 0x006B04u, func_006B04 },
    { 0x01C822u, func_01C822 },
    { 0x0725CAu, func_0725CA },
    { 0x01B67Cu, func_01B67C },
    { 0x01C6B8u, func_01C6B8 },
    { 0x001F7Cu, func_001F7C },
    { 0x001DB2u, func_001DB2 },
    { 0x001DACu, func_001DAC },
    { 0x001ED0u, func_001ED0 },
    { 0x006C7Eu, func_006C7E },
    { 0x001E52u, func_001E52 },
    { 0x0068B2u, func_0068B2 },
    { 0x072878u, func_072878 },
    { 0x071CCAu, func_071CCA },
    { 0x072850u, func_072850 },
#endif  /* leaf-level tests */

    /* === Stack-level tests: caller functions that exercise sub-call chains === */
    /* These test the CALLING FUNCTION as a unit — native runs the entire
     * subtree (all JSR calls), interpreter runs real 68K code.  Catches
     * inter-function ordering bugs like Sonic_Jump → Sonic_Move. */
    /* Stack-level test */
    { 0x012E18u, func_012E18 },   /* Obj01_MdNormal (full chain) */
};

/* Oracle build runs EVERYTHING through the interpreter (no native overrides).
 * Native debug builds use the curated table for stack-level divergence tests. */
#ifdef SONIC_ORACLE_BUILD
int g_hybrid_table_size = 0;
#else
int g_hybrid_table_size = 1;
#endif
