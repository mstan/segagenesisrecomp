/* No desktop dialog processes on Switch. Returning -1 selects recomp-ui's
 * existing controller-operated browser, including optional donor images. */
#include "launcher_files.h"
bool launcher_native_file_picker_available(void) { return false; }
int launcher_try_pick_file(const char *title, const char *const *patterns,
    int count, const char *desc, char *out, size_t cap) { return -1; }
int launcher_try_pick_folder(const char *title, char *out, size_t cap) { return -1; }
int launcher_try_pick_save_file(const char *title, const char *const *patterns,
    int count, const char *desc, char *out, size_t cap) { return -1; }
bool launcher_pick_rom(char *out, size_t cap) { return false; }
bool launcher_pick_file(const char *title, const char *const *patterns,
    int count, const char *desc, char *out, size_t cap) { return false; }
bool launcher_pick_folder(const char *title, char *out, size_t cap) { return false; }
bool launcher_pick_save_file(const char *title, const char *const *patterns,
    int count, const char *desc, char *out, size_t cap) { return false; }
bool launcher_file_picker_selftest(void) { return false; }
