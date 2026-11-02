/* Compile the real emitter's output; no private ROM or source-text assertion. */
#include "game_config.h"
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv)
{
    if(argc!=3)return 2;
    GameConfig *cfg=(GameConfig *)calloc(1,sizeof *cfg);
    if(!cfg)return 1;
    cfg->ram_layout.present=true;
    if(!strcmp(argv[1],"filled")) {
        cfg->ram_layout.level_mode_count=2;
        cfg->ram_layout.level_modes[0]=4;cfg->ram_layout.level_modes[1]=8;
        cfg->ram_layout.ws_eligible_mode_count=1;
        cfg->ram_layout.ws_eligible_modes[0]=12;
    }
    int result=game_config_emit_layout(cfg,argv[2])?0:1;
    free(cfg);return result;
}
