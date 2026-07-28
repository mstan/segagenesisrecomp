/*
 * backend_decls.h — own-backend (native/release) replacement for the
 * clownmdemu.h include. The native targets compile with NO clownmdemu-core
 * include paths (the AGPL-purity enforcement — see each game repo's
 * CMakeLists); shared runner code that still carries oracle-only call sites
 * gets the few names it needs from here instead.
 *
 * Clean-room: this header declares interface facts only — an opaque emulator
 * type for pointer-typed parameters that are never dereferenced in native
 * builds, and the Sega Genesis 3-button pad set (hardware facts) under the
 * enumerator names the shared input code already uses. No clownmdemu text.
 */
#ifndef BACKEND_DECLS_H
#define BACKEND_DECLS_H

#include "clowncommon.h"   /* integer typedefs — ISC-permissive, vendored at
                              runner/external/clowncommon */

#endif /* BACKEND_DECLS_H */
