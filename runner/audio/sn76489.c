#include "sn76489.h"
#include "genesis_clocks.h"
#include <sega_psg.h>
#include <stdlib.h>
static sega_psg *instance;
static sega_psg *get_psg(void) {
 if(!instance)instance=sega_psg_create(SEGA_PSG_GENESIS,GENESIS_MASTER_CLOCK_NTSC);
 if(!instance){fputs("Unable to create shared Genesis PSG\n",stderr);abort();}
 return instance;
}
void psg_init(void){sega_psg_reset(get_psg());}
void psg_advance(uint32_t n){sega_psg_advance(get_psg(),n);}
void psg_write(uint8_t v){sega_psg_write(get_psg(),v);}
size_t psg_render(int16_t *out,size_t n){return sega_psg_render(get_psg(),out,n);}
size_t psg_render_pair(int16_t *f,int16_t *raw,size_t n){return sega_psg_render_pair(get_psg(),f,raw,n);}
void psg_set_audition_channels(unsigned m){sega_psg_audition_channels(get_psg(),m);}
size_t psg_peek_audition(int16_t *f,int16_t *raw,size_t n){return sega_psg_peek_audition(get_psg(),f,raw,n);}
size_t psg_samples_available(void){return sega_psg_available(get_psg());}
uint32_t psg_sample_rate(void){return sega_psg_sample_rate(get_psg());}
void psg_reset_leftover(void){sega_psg_reset_leftover(get_psg());}
int psg_save_state(FILE *f){return sega_psg_save(get_psg(),f);}
int psg_load_state(FILE *f){return sega_psg_load(get_psg(),f);}
size_t psg_rb_save(void *o,size_t n){return sega_psg_rb_save(get_psg(),o,n);}
int psg_rb_load(const void *i,size_t n){return sega_psg_rb_load(get_psg(),i,n);}
uint64_t psg_cosim_hash(void){return sega_psg_state_hash(get_psg());}
#ifdef GENESIS_COSIM
int psg_cosim_dump(char *o,int n){return sega_psg_state_dump(get_psg(),o,n);}
#endif
