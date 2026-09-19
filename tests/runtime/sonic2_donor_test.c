#include "../../sonicthehedgehog2/sonic2_donor_assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"line %d: %s\n",__LINE__,#c); exit(1); } } while (0)
int main(void)
{
    uint8_t rom[256]={0}; char error[160]; S2DonorBank bank={0};
    S2DonorLayout layout={1,0,2,128,64,6};
    rom[1]=4; /* mapping ->4 */ rom[3]=20; /* DPLC ->22 */
    rom[5]=1; /* one mapping piece */ rom[6]=0xFC; /* y=-4 */
    rom[10]=0xFF;rom[11]=0xFC; /* x=-4, 8x8 tile 0 */
    rom[23]=1; /* one DPLC cue, source tile 0, length 1 */
    memset(rom+128,0x12,32);
    CHECK(s2_donor_decode(rom,sizeof rom,&layout,&bank,error,sizeof error));
    CHECK(bank.count==1 && bank.frames[0].width==8 && bank.frames[0].height==8);
    CHECK(bank.frames[0].x==-4 && bank.frames[0].y==-4);
    CHECK(bank.frames[0].pixels[0]==1 && bank.frames[0].pixels[1]==2);
    rom[8]=8; /* horizontal flip */
    CHECK(s2_donor_decode(rom,sizeof rom,&layout,&bank,error,sizeof error));
    CHECK(bank.frames[0].pixels[0]==2 && bank.frames[0].pixels[1]==1);
    uint8_t *previous=bank.frames[0].pixels;
    rom[9]=2; /* references missing tile */
    CHECK(!s2_donor_decode(rom,sizeof rom,&layout,&bank,error,sizeof error));
    CHECK(bank.frames[0].pixels==previous && bank.frames[0].pixels[0]==2);
    CHECK(!s2_donor_decode(rom,10,&layout,&bank,error,sizeof error));
    rom[9]=0; rom[5]=255;
    CHECK(!s2_donor_decode(rom,sizeof rom,&layout,&bank,error,sizeof error));
    s2_donor_free(&bank); CHECK(!bank.count && !bank.frames[0].pixels);
    puts("sonic2_donor: mapping/DPLC bounds, flip, geometry and transactional failure OK");
    return 0;
}
