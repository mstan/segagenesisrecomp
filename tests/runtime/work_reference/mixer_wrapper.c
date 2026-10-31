/* Preserve the frozen body; only select a separate diagnostic output path.
 * Include libc before the macro so its imported getenv declaration is intact. */
#include <stdlib.h>
char *ref_getenv(const char *);
#define getenv ref_getenv
#include "mixer.c"
