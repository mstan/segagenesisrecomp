/* Fixture dispatch wrapper around ACTUAL SmsRecomp-generated guarded code. */
#include "z80_recomp.h"
#include "proof_control_step.h"
#include "proof_variants_step.h"
extern int force_fallback,variant_fixture;
void adapter_dispatch_step(void)
{if(force_fallback)sms_dispatch_miss(g_z80.pc);else if(variant_fixture)proof_variants_step();else proof_control_step();}
