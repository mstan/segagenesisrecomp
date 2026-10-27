/* Optional title-owned services installed before the first CPU operation.
 * Implementations may prepend guarded modules; unsupported requests must
 * execute the original operation. No CPU/device clock or pacing override. */
#ifndef SEGACD_STANDALONE_SERVICES_H
#define SEGACD_STANDALONE_SERVICES_H
#include "machine.h"
#include "disc_image.h"
void segacd_standalone_services_install(SegaCDMachine *, const SegaCDImage *);
#endif
