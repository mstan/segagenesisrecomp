/* Compatibility names for the shared portable streaming hash. */
#ifndef SEGACD_SHA256_H
#define SEGACD_SHA256_H
#include <recomp_mod/sha256.h>
typedef rmod_sha256_state Sha256;
static inline void sha_init(Sha256 *state) { rmod_sha256_init(state); }
static inline void sha_update(Sha256 *state, const uint8_t *data, size_t size) {
    rmod_sha256_update(state, data, size);
}
static inline void sha_final(Sha256 *state, uint8_t out[32]) { rmod_sha256_final(state, out); }
#endif
