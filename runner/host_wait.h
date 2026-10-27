#ifndef GENESIS_HOST_WAIT_H
#define GENESIS_HOST_WAIT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Host presentation only: no clock, handle or selected path enters simulation. */
void genesis_host_wait_until(uint64_t deadline, uint64_t frequency);
void genesis_host_wait_shutdown(void);
int genesis_host_wait_native_active(void);
#ifdef __cplusplus
}
#endif
#endif
