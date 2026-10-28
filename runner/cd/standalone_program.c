/* Build-private guarded catalogs. No owned bytes are kept in this source. */
#include "machine.h"
#include "segacd_bios.h"
#include "segacd_main_ram.h"
#include "segacd_sub.h"
#include "segacd_bios_extra_modules.h"
#include "sonic_cd_z80_step.h"
#include <string.h>
#ifdef GENESIS_COMPACT_NATIVE_CATALOGS
#include "native_catalog_registry.h"
#endif
#ifdef SEGACD_BIOS_PROBE_SUB_PATCH
#include "segacd_sub_patch.h"
#endif
#ifdef SEGACD_BIOS_PROBE_BOOT_MAIN
#include "segacd_boot_main.h"
#endif
#ifdef SEGACD_BIOS_PROBE_BOOT_SUB
#include "segacd_boot_sub.h"
#endif
/* Identity of the currently qualified owned inputs, not embedded media.
 * Other revisions require their own generated/validated native catalogs. */
int segacd_standalone_accept_bios(const uint8_t *bytes,size_t size)
{
    if(!bytes||size!=SEGACD_BIOS_BYTES)return 0;
    uint32_t crc=0xffffffffu;
    for(size_t i=0;i<size;++i) {
        crc^=bytes[i];
        for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u);
    }
    return (crc^0xffffffffu)==0xc6d10268u;
}
int segacd_standalone_accept_disc(const uint8_t *sha256)
{
    static const uint8_t qualified[32]={
        0x4e,0x82,0x20,0x10,0xe4,0x20,0x92,0x68,0x1b,0x23,0x8d,0xdc,0x7c,0xbf,0xf4,0x18,
        0x02,0xc0,0xcf,0xc0,0x06,0xc9,0xb1,0x51,0x0d,0xce,0xbd,0x6b,0x9d,0xd4,0x52,0xb1};
    return sha256&&!memcmp(qualified,sha256,sizeof qualified);
}
const SegaCDNativeProgram *segacd_standalone_program(void)
{
#ifdef GENESIS_COMPACT_NATIVE_CATALOGS
    static const M68kStepModule *const main_modules[]={GENESIS_CATALOG_MAIN_MODULES};
    static const M68kStepModule *const sub_modules[]={GENESIS_CATALOG_SUB_MODULES};
#else
    static const M68kStepModule *const main_modules[]={&segacd_bios_module,&segacd_main_ram_module
#ifdef SEGACD_BIOS_PROBE_BOOT_MAIN
        ,&segacd_boot_main_module
#endif
        SEGACD_BIOS_PROBE_EXTRA_MAIN_MODULES
    };
    static const M68kStepModule *const sub_modules[]={&segacd_sub_module
#ifdef SEGACD_BIOS_PROBE_SUB_PATCH
        ,&segacd_sub_patch_module
#endif
#ifdef SEGACD_BIOS_PROBE_BOOT_SUB
        ,&segacd_boot_sub_module
#endif
        SEGACD_BIOS_PROBE_EXTRA_SUB_MODULES
    };
#endif
    static const SegaCDNativeProgram program={main_modules,sub_modules,
        sizeof main_modules/sizeof *main_modules,sizeof sub_modules/sizeof *sub_modules,sonic_cd_z80_step};
    return &program;
}
