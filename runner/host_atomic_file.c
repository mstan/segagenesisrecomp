#include "host_atomic_file.h"
#include <recomp_persistence/atomic_file.h>
int genesis_atomic_file_store(const char *path, const void *data, size_t size) {
    return recomp_atomic_file_store(path, data, size);
}
