#include "local_states.h"
#include "game_spec.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t guest;
static uint32_t presentation;
static size_t presentation_bytes=sizeof presentation;
static int reject_presentation,loaded_calls;
static void reset_presentation(void){presentation=0;}
static void state_loaded(void){++loaded_calls;presentation=0;}
static size_t presentation_save(void *dst,size_t cap)
{
    if(!dst)return presentation_bytes;
    if(cap<presentation_bytes)return 0;
    memset(dst,0xCC,presentation_bytes);memcpy(dst,&presentation,sizeof presentation);
    return presentation_bytes;
}
static int presentation_load(const void *src,size_t size,int apply)
{
    if(reject_presentation || size<sizeof presentation)return 0;
    if(apply)memcpy(&presentation,src,sizeof presentation);
    return 1;
}
static const GameVideo video={.presentation_reset=reset_presentation,
    .presentation_state_save=presentation_save,.presentation_state_load=presentation_load};
const GameSpec g_game_spec={.video=&video,.on_state_loaded=state_loaded};
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
    reset_presentation(); /* Actual raw RB loads invalidate caches. */
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
    if (argc < 2 || argc > 3) return 2;
    uint32_t frame[16] = {0};
    int fails = 0;
    genesis_local_states_configure(argv[1], 1, 4, 1);
    for (guest = 1; guest <= 3; ++guest) {
        frame[0] = 0xFF000000u | guest;
        presentation=guest*10;
        genesis_local_states_note_frame(frame, 4, 4, 4);
    }
    fails += check(genesis_local_states_open_rewind(), "rewind did not open");
    genesis_local_states_move(-1);
    uint32_t before_reject=guest;
    reject_presentation=1;
    fails += check(!genesis_local_states_accept() && guest==before_reject && presentation==30 && loaded_calls==0,
                   "rejected presentation mutated the live guest/cache");
    reject_presentation=0;
    fails += check(genesis_local_states_accept() && guest == 2 && presentation==20 && loaded_calls==1,
                   "rewind did not restore previous snapshot");
    guest = 4;
    presentation=40;
    genesis_local_states_note_frame(frame, 4, 4, 4);
    fails += check(genesis_local_states_open_rewind(), "branch did not retain history");
    genesis_local_states_move(-1);
    fails += check(genesis_local_states_accept() && guest == 2 && presentation==20 && loaded_calls==2,
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
    genesis_local_states_clear();
    presentation_bytes=65536;
    for(guest=1;guest<=3;++guest)genesis_local_states_note_frame(frame,4,4,4);
    fails+=check(genesis_local_states_history_bytes()<=GENESIS_LOCAL_HISTORY_LIMIT,
                 "presentation bytes escaped the history budget");
    fails+=check(!genesis_local_states_open_rewind(),"budget did not retire oldest companions");
    genesis_local_states_clear();presentation_bytes=sizeof presentation;
    uint32_t wide[64],thumb[128*90]={0};for(unsigned i=0;i<64;++i)wide[i]=0xFF123456u;
    genesis_local_states_note_frame(wide,16,16,4);genesis_local_states_slot_saved(1);
    char path[1024];snprintf(path,sizeof path,"%snative_save_1.bin.thumb",argv[1]);
    FILE *tf=fopen(path,"rb");char magic[8];
    fails+=check(tf && fread(magic,1,8,tf)==8 && fread(thumb,sizeof *thumb,128*90,tf)==128*90,
                 "aspect thumbnail missing");
    if(tf)fclose(tf);
    fails+=check(thumb[0]==0xFF000000u && thumb[29*128]==0xFF123456u &&
                 thumb[60*128+127]==0xFF123456u && thumb[61*128]==0xFF000000u,
                 "wide thumbnail was stretched instead of letterboxed");
    genesis_local_states_note_frame(wide,16,16,4);
    fails+=check(genesis_local_states_open_rewind(),"aspect rewind browser did not open");
    fails+=check(genesis_local_states_overlay(&panel,&w,&h) && panel && w==640 && h==448,
                 "aspect rewind browser did not render");
    /* Check the final production panel, including its141x99 preview scale.
     * A four-to-one image must remain a short horizontal strip; a correct
     * saved thumbnail alone would not catch a stretched browser widget. */
    if(panel && w==640 && h==448) {
        for(int y=335;y<432;++y)
            fails+=check(panel[y*640+90]==(y>=366&&y<=401?0xFF123456u:0xFF000000u),
                         "rewind panel distorted the wide preview");
        if(argc>2) {
            FILE *picture=fopen(argv[2],"wb");
            fails+=check(picture!=NULL,"could not export requested panel evidence");
            if(picture) {
                fprintf(picture,"P6\n640 448\n255\n");
                for(int i=0;i<w*h;i++) {
                    unsigned char rgb[3]={(unsigned char)(panel[i]>>16),
                        (unsigned char)(panel[i]>>8),(unsigned char)panel[i]};
                    fwrite(rgb,1,3,picture);
                }
                fclose(picture);
            }
        }
    }
    genesis_local_states_shutdown();
    return fails ? 1 : 0;
}
