#ifndef SEGACD_BIOS_SERVICES_H
#define SEGACD_BIOS_SERVICES_H
#include "segacd_coordinator.h"
/* Install reusable firmware services, not game-address exceptions. Derived
 * dispatch state follows the existing one-active-machine host contract. */
void segacd_bios_services_install(SegaCDCoordinator *,const void *bios,size_t bytes);
void segacd_bios_services_stats(uint64_t *completed,uint64_t *declined);
#endif
