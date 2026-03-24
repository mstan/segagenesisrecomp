/*
 * extras.h — Sonic the Hedgehog extras declarations.
 * Implements game_extras.h for Sonic the Hedgehog (Genesis, 1991).
 */
#pragma once
#include "game_extras.h"

/* Recompiled entry points (defined in generated/sonic_full.c) */
void func_000206(void);   /* EntryPoint     ($000206) */
void func_000B10(void);   /* VBlank IRQ6    ($000B10) */
void func_001126(void);   /* HBlank IRQ4    ($001126) */
