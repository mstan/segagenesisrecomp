#include "../../runner/input_script.h"
#include "genesis_runtime.h"
#include "crash_report.h"
#include <stdio.h>
#include <stdlib.h>
M68KState g_cpu;
void crash_report_dump_persistent(const char *r,const M68KState *c,uint32_t a,int w,uint64_t f)
{ (void)c;(void)a;(void)w;(void)f; fprintf(stderr,"%s\n",r); exit(1); }
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"line %d: %s\n",__LINE__,#c); exit(1); } } while (0)
static void tick(unsigned frame) { input_script_tick(frame,NULL,NULL,NULL,NULL,NULL); }
int main(int argc,char **argv)
{
    CHECK(argc == 2 && input_script_load(argv[1]));
    tick(0);
    CHECK(input_script_held_mask()==8 && input_script_player_mask(1)==4);
    CHECK(input_script_player_mask(2)==16 && input_script_player_mask(3)==32);
    CHECK(!input_script_player_mask(-1) && !input_script_player_mask(4));
    CHECK(input_script_player_used(3));
    tick(1); CHECK(input_script_player_mask(2)==16 && input_script_player_mask(3)==32);
    tick(2); CHECK(!input_script_player_mask(2) && input_script_player_mask(3)==32);
    tick(3); CHECK(!input_script_held_mask() && input_script_player_mask(1)==4);
    tick(4); CHECK(!input_script_player_mask(3) && input_script_player_mask(1)==4);
    input_script_load(NULL);
    CHECK(!input_script_player_used(3) && !input_script_player_mask(1));
    puts("input_script_party: independent four-player masks and release timers OK");
    return 0;
}
