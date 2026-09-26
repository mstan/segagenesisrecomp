#ifndef GENESIS_NETPLAY_IDENTITY_H
#define GENESIS_NETPLAY_IDENTITY_H
/* genesis_netplay_identity.h -- build / content identity (see the .c). */
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void        genesis_identity_sha256(const uint8_t *data, size_t len, uint8_t out[32]);
void        genesis_identity_hex(const uint8_t *bytes, size_t n, char *out);  /* out: 2n+1 */
uint32_t    genesis_identity_build_fp(void);
/* "<release>+<build fp hex>", static storage. */
const char *genesis_identity_game_version(const char *release);

#ifdef __cplusplus
}
#endif

#endif
