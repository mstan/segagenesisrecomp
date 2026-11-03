#include "genesis_raster_jobs.h"
#include "genesis_machine.h"
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do{if(!(c)){fprintf(stderr,"line%d: %s\n",__LINE__,#c);return 1;}}while(0)
/* The real machine save/load and pointer wiring are linked. Scheduling and bus
 * execution are outside this restore-owner test and must never be invoked. */
unsigned long g_snd_frame;
unsigned g_snd_line;
uint16_t gbus_read16(GenesisBus *b,uint32_t a){(void)b;(void)a;abort();}
uint8_t gbus_z80_read(GenesisBus *b,uint16_t a){(void)b;(void)a;abort();}
void gbus_z80_write(GenesisBus *b,uint16_t a,uint8_t v){(void)b;(void)a;(void)v;abort();}
void gbus_init(GenesisBus *b,GVDP *v){(void)b;(void)v;abort();}
void z80_init(z80 *z){(void)z;abort();}
void z80_step(z80 *z){(void)z;abort();}
void glue_run_game_chunk(uint32_t c){(void)c;abort();}
void glue_own_interrupt(int l,GVDP *v){(void)l;(void)v;abort();}
int glue_own_vint_service_latched(GVDP *v){(void)v;abort();}
int glue_own_hint_service_latched(GVDP *v){(void)v;abort();}
static uint32_t pixels[GVDP_MAX_WIDTH*GVDP_HD_ROWS];
static const GVDPRasterFrame *publish(void)
{
    uint8_t row[GVDP_MAX_WIDTH];uint32_t palette[256];
    for(unsigned i=0;i<256;++i)palette[i]=0xff102030u;
    for(unsigned y=0;y<224;++y){gvdp_render_scanline(&g_machine.vdp,(int)y,row);gvdp_hd_bind_palette(y,palette,256);}
    return gvdp_hd_acquire();
}
int main(void)
{
    gvdp_hd_enable(1);memset(&g_machine,0,sizeof g_machine);gvdp_init(&g_machine.vdp);
    const GVDPRasterFrame *old=publish();CHECK(old);
    FILE *f=tmpfile();CHECK(f&&machine_save_state(f));g_machine.vdp.vram[0]=55;rewind(f);
    CHECK(machine_load_state(f));CHECK(g_machine.vdp.vram[0]==0);CHECK(!gvdp_hd_acquire());fclose(f);
    CHECK(gvdp_hd_render_argb(old,pixels,GVDP_MAX_WIDTH));CHECK(pixels[0]==0xff102030u);gvdp_hd_release(old);
    old=publish();CHECK(old);f=tmpfile();CHECK(f);CHECK(fputc(0x99,f)!=EOF);rewind(f);
    CHECK(!machine_load_state(f));CHECK(!gvdp_hd_acquire());fclose(f);
    CHECK(gvdp_hd_render_argb(old,pixels,GVDP_MAX_WIDTH));CHECK(pixels[0]==0xff102030u);gvdp_hd_release(old);
    puts("Direct machine_load_state valid+partial failure revoke HD publication; held jobs remain exact");return 0;
}
