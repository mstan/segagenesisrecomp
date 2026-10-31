#include "game_layout.h"
#include <stdio.h>
extern const GameRamLayout empty_game_layout,filled_game_layout;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"generated layout line%u: %s\n",(unsigned)__LINE__,#x);return 1;}}while(0)
int main(void)
{
    CHECK(empty_game_layout.level_mode_count==0);
    CHECK(empty_game_layout.ws_eligible_mode_count==0);
    CHECK(filled_game_layout.level_mode_count==2);
    CHECK(filled_game_layout.level_modes[0]==4 && filled_game_layout.level_modes[1]==8);
    CHECK(filled_game_layout.ws_eligible_mode_count==1 && filled_game_layout.ws_eligible_modes[0]==12);
    for(unsigned i=0;i<GAME_LAYOUT_LEVEL_MODES_MAX;++i) {
        CHECK(empty_game_layout.level_modes[i]==0 && empty_game_layout.ws_eligible_modes[i]==0);
        if(i>=2)CHECK(filled_game_layout.level_modes[i]==0);
        if(i>=1)CHECK(filled_game_layout.ws_eligible_modes[i]==0);
    }
    puts("Generated empty and populated C11 layouts compile and preserve their array values/counts");return 0;
}
