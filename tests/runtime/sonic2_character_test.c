#include "sonic2_character.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int16_t sine(unsigned a)
{ static const int16_t quarters[]={0,256,0,-256}; assert(!(a&63)); return quarters[(a&255)/64]; }
static S2Motion standing(void)
{ S2Motion m={0}; m.radius_x=9; m.radius_y=19; m.animation=5; return m; }
int main(void)
{
    S2Contacts c={64,64,64,sine}; S2CharacterState s; S2Motion m=standing();
    s2_character_reset(&s,S2_CHAR_AMY);
    s2_character_before(&s,&m,0x42,0x40,&c);
    assert(s.native_pressed==0x20 && s.jump_adjustment==-0x250);
    m.status=6; m.jumping=1; m.vy=-0x680;
    s2_character_after(&s,&m,&c);
    assert(m.vy==-0x8D0 && m.animation==0x26 && !s2_character_attacking(&s,&m));
    s2_character_reset(&s,S2_CHAR_AMY); m=standing();
    s2_character_before(&s,&m,0x20,0x20,&c);
    m.status=6; m.jumping=1; m.vy=-0x680; m.animation=2;
    s2_character_after(&s,&m,&c);
    assert(m.animation==0x10 && !s2_character_attacking(&s,&m));
    s2_character_reset(&s,S2_CHAR_AMY); m=standing();
    s2_character_before(&s,&m,0x40,0x40,&c); s2_character_after(&s,&m,&c);
    assert(m.animation==0x28 && s2_character_attacking(&s,&m) && !s.native_pressed);
    s2_character_reset(&s,S2_CHAR_AMY); m=standing();
    s2_character_before(&s,&m,0x22,0x20,&c);
    assert(m.vx==0x900 && m.vy==0x380 && !(s.native_held&2));
    s2_character_reset(&s,S2_CHAR_AMY); m=standing();
    s2_character_before(&s,&m,0x21,0x20,&c);
    assert(s.move==S2_MOVE_FIXED && s.charge==0);
    s2_character_before(&s,&m,0x21,0x20,&c);
    assert(s.charge==0x200);
    s2_character_before(&s,&m,0,0,&c); assert(m.inertia==0x900);
    s2_character_reset(&s,S2_CHAR_KNUCKLES); m=standing();
    s2_character_before(&s,&m,0x20,0x20,&c);
    m.status=6; m.jumping=1; m.vy=-0x680;
    s2_character_after(&s,&m,&c); assert(m.vy==-0x600);
    m.vy=-0x380;
    s2_character_before(&s,&m,0x20,0x20,&c);
    assert(s.special==S2_SK_GLIDE && s.move==S2_MOVE_AIR && m.radius_y==10 && !(m.status&4));
    assert(s2_character_attacking(&s,&m));
    s2_character_before(&s,&m,0,0,&c);
    assert(s.special==S2_SK_FALL && m.radius_y==19);
    s.special=S2_SK_CLIMB; s.wall_x=m.x; c.wall=0;
    s2_character_before(&s,&m,1,0,&c); assert(m.y==-65536);
    s2_character_before(&s,&m,0x20,0x20,&c);
    assert(s.special==S2_SK_NORMAL && m.vx==-0x400 && m.vy==-0x380 && (m.status&4));
    S2DonorBank bank={0}; bank.count=253; bank.animation_count=6;
    bank.animation_length[5]=5; memcpy(bank.animations[5],(uint8_t[]){1,10,11,0xFE,1},5);
    s2_character_reset(&s,S2_CHAR_AMY); m=standing();
    s2_character_animate(&s,&m,&bank); assert(m.frame==10);
    s2_character_animate(&s,&m,&bank); assert(m.frame==10);
    s2_character_animate(&s,&m,&bank); assert(m.frame==11);
    s2_character_animate(&s,&m,&bank); s2_character_animate(&s,&m,&bank); assert(m.frame==11);
    puts("Source-derived Amy/Knuckles action transitions and animation control codes passed");
    return 0;
}
