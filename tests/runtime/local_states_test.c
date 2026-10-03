#include "local_states.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t guest;
size_t genesis_rb_bound(void) { return sizeof guest; }
size_t genesis_rb_save(void *dst, size_t cap)
{
    if (cap < sizeof guest) return 0;
    *(uint32_t *)dst = guest;
    return sizeof guest;
}
int genesis_rb_load(const void *src, size_t len)
{
    if (len != sizeof guest) return 0;
    guest = *(const uint32_t *)src;
    return 1;
}
int runner_save_state_file(const char *path)
{
    FILE *f = fopen(path, "wb");
    int ok = f && fwrite(&guest, sizeof guest, 1, f) == 1;
    if (f) fclose(f);
    return ok;
}
int runner_load_state_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    int ok = f && fread(&guest, sizeof guest, 1, f) == 1;
    if (f) fclose(f);
    return ok;
}
static int check(int truth, const char *message)
{
    if (truth) return 0;
    fprintf(stderr, "local_states: %s\n", message);
    return 1;
}
int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    uint32_t frame[16] = {0};
    int fails = 0;
    genesis_local_states_configure(argv[1], 1, 4, 1);
    for (guest = 1; guest <= 3; ++guest) {
        frame[0] = 0xFF000000u | guest;
        genesis_local_states_note_frame(frame, 4, 4, 4);
    }
    fails += check(genesis_local_states_open_rewind(), "rewind did not open");
    genesis_local_states_move(-1);
    fails += check(genesis_local_states_accept() && guest == 2,
                   "rewind did not restore previous snapshot");
    guest = 4;
    genesis_local_states_note_frame(frame, 4, 4, 4);
    fails += check(genesis_local_states_open_rewind(), "branch did not retain history");
    genesis_local_states_move(-1);
    fails += check(genesis_local_states_accept() && guest == 2,
                   "branch retained abandoned future");
    guest = 7;
    genesis_local_states_note_frame(frame, 4, 4, 4);
    fails += check(genesis_local_states_open_menu(), "save browser did not open");
    fails += check(genesis_local_states_save(), "save browser could not save");
    const uint32_t *panel = NULL;
    int w = 0, h = 0;
    fails += check(genesis_local_states_overlay(&panel, &w, &h) &&
                   panel && w == 640 && h == 448, "save browser did not render");
    guest = 9;
    fails += check(genesis_local_states_accept() && guest == 7,
                   "save browser could not load saved slot");
    genesis_local_states_shutdown();
    return fails ? 1 : 0;
}
