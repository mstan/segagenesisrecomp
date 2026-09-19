/* Sonic 2 host adapter. Stock object execution owns the world tick. These
 * callbacks adapt player roles only, never replay a level or enemy update. */
#include "sonic2_runtime.h"
#include "sonic2_party.h"
#include "sonic2_resources.h"
#include "sonic2_character.h"
#include "sonic2_video.h"
#include "video/genesis_vdp.h"
#include "video/genesis_dac.h"
#include "genesis_runtime.h"
#include "input_map.h"
#include "input_script.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#if GENESIS_HAS_RECOMP_NET
#include "netplay/genesis_netplay.h"
#endif
static int active, inside_player, actor = -1;
static uint8_t previous_input[4];
static unsigned outside[4];
static S2CharacterState characters[4];
static uint8_t physics[4][6];
static unsigned objects[4]={0xB000,0xB040,0,0};
static S2DonorBank native_banks[2];
/* The native status byte only has standing/pushing bits for two players.
 * Extra actors retain those two bits per solid on the host. The actual actor
 * objects have real, reserved native pool addresses, so monitor/platform
 * parent links stay valid across ticks (no temporary Sidekick alias). */
static uint8_t solid_flags[2][144], solid_ids[144];
static int inside_solid;
static int amy_available(void) { return s2_party.amy_enabled && s2_resource_verified(S2_RESOURCE_AMY); }
static int knuckles_available(void) { return s2_party.s3k_enabled && s2_resource_verified(S2_RESOURCE_SK); }
static const S2Character amy={"amy","AMY",1,amy_available};
static const S2Character knuckles={"knuckles","KNUCKLES",1,knuckles_available};
static unsigned kind(unsigned p)
{
    const char *id=s2_party.roster.character[p];
    return !strcmp(id,"amy")?S2_CHAR_AMY:!strcmp(id,"knuckles")?S2_CHAR_KNUCKLES:
        !strcmp(id,"tails")?S2_CHAR_TAILS:S2_CHAR_SONIC;
}
static const S2DonorBank *bank(unsigned p)
{
    unsigned k=kind(p);
    return k<S2_CHAR_AMY?&native_banks[k]:s2_resource_bank(k==S2_CHAR_AMY?S2_RESOURCE_AMY:S2_RESOURCE_SK);
}
static int imported(void) { return actor>=0 && kind((unsigned)actor)>=S2_CHAR_AMY; }
void s2_runtime_load(void)
{
    active=inside_player=0; actor=-1;
    objects[2]=objects[3]=0;
    s2_donor_free(&native_banks[0]); s2_donor_free(&native_banks[1]);
    s2_character_register(&knuckles); s2_character_register(&amy);
}
static uint16_t word(unsigned a) { return (uint16_t)((g_ram[a] << 8) | g_ram[a+1]); }
static void putword(unsigned a, unsigned v) { g_ram[a]=(uint8_t)(v>>8); g_ram[a+1]=(uint8_t)v; }
static int32_t fixed(unsigned a) { return (int32_t)(((uint32_t)word(a)<<16)|word(a+2)); }
static void putfixed(unsigned a, int32_t v) { putword(a,(uint32_t)v>>16); putword(a+2,(uint32_t)v); }
static int16_t sine(unsigned a) { a=0x33CE+(a&255)*2; return (int16_t)((g_rom[a]<<8)|g_rom[a+1]); }
static S2Motion motion(unsigned o)
{
    S2Motion m={0}; m.x=fixed(o+8); m.y=fixed(o+12);
    m.vx=(int16_t)word(o+0x10); m.vy=(int16_t)word(o+0x12); m.inertia=(int16_t)word(o+0x14);
    m.status=g_ram[o+0x22]; m.angle=g_ram[o+0x26]; m.radius_y=g_ram[o+0x16]; m.radius_x=g_ram[o+0x17];
    m.animation=g_ram[o+0x1C]; m.frame=g_ram[o+0x1A]; m.jumping=g_ram[o+0x3C];
    m.hurt=g_ram[o+0x24]>=4; m.boosted=word(o+0x34)!=0; m.control_locked=!!(g_ram[o+0x2A]&1);
    return m;
}
static void store_motion(unsigned o, const S2Motion *m)
{
    putfixed(o+8,m->x); putfixed(o+12,m->y); putword(o+0x10,m->vx); putword(o+0x12,m->vy); putword(o+0x14,m->inertia);
    g_ram[o+0x22]=m->status; g_ram[o+0x26]=m->angle; g_ram[o+0x16]=m->radius_y; g_ram[o+0x17]=m->radius_x;
    g_ram[o+0x1C]=m->animation; g_ram[o+0x1A]=m->frame; g_ram[o+0x3C]=m->jumping;
}
static M68KState native_helper(uint32_t pc)
{
    /* A real, balanced synthetic return slot for native helpers. Their nested
     * calls retain normal strict-stack checks; no generated C is edited. */
    M68KState before=g_cpu;
    recomp_push_return(0x15FDC); recomp_call_addr(pc);
    M68KState result=g_cpu; g_cpu=before; return result;
}
static int16_t host_call(uint32_t pc) { return (int16_t)native_helper(pc).D[1]; }
static S2Contacts contacts(unsigned o)
{
    S2Contacts c={64,64,64,sine};
    if (!g_ram[o+0x16]) return c;
    uint8_t angles[4]; memcpy(angles,g_ram+0xF768,4);
    uint32_t d5=g_cpu.D[5];
    c.floor=host_call(0x1EC4E); /* also selects the player's collision plane */
    g_cpu.D[5]=g_ram[o+0x3F];
    c.wall=host_call((g_ram[o+0x22]&1)?0x1F05E:0x1EEDC);
    c.ceiling=host_call(0x1EF2E);
    g_cpu.D[5]=d5; memcpy(g_ram+0xF768,angles,4); return c;
}
static int vanilla(void)
{
    const S2Roster *r=&s2_party.roster;
    return r->slots==2 && !strcmp(r->character[0],"sonic") && !strcmp(r->character[1],"tails");
}
const char *s2_runtime_state_unavailable_reason(void)
{
    return vanilla()?NULL:"Experimental party controllers are not serialized by machine quickstates; use a fresh native game.";
}
static int level(void) { return active && ((g_ram[0xF600]&0x7F)==12); }
static int special_stage(void) { return active && g_ram[0xF600]==16; }
static unsigned native_id(unsigned p)
{
    const S2Character *c=s2_character_find(s2_party.roster.character[p]);
    return c && c->available() ? c->native_object_id : 0;
}
static uint8_t companion_input(unsigned p, unsigned object)
{
    uint8_t held;
    if (input_player_connected(p) || input_script_player_used(p))
        held=(uint8_t)(input_current_mask(p) | input_script_player_mask(p));
    else {
        int dx=(int16_t)(word(0xB008)-word(object+8));
        int dy=(int16_t)(word(0xB00C)-word(object+12));
        held=dx>24?8:dx < -48?4:0;
        /* Companion CPU is independent of the character controller. It can
         * traverse simple terrain without secretly taking over a connected pad. */
        if ((g_ram[0xB022]&2) || (dy < -20 && !(g_ram[object+0x22]&2)) ||
            ((g_ram[object+0x22]&0x20) && !(word(0xFE04)&31))) held|=0x20;
    }
    return held;
}
static void respawn(unsigned object, unsigned p)
{
    /* CPU/local companions share P1's checkpoint and never spend a life. */
    memset(g_ram+object,0,0x40);
    g_ram[object]=(uint8_t)native_id(p);
    putword(object+8,word(0xB008)-24*p);
    putword(object+12,word(0xB00C)-16);
    g_ram[object+0x22]=2;
    putword(object+0x30,120);
    outside[p]=0;
    s2_character_reset(&characters[p],kind(p));
}
static void reserve_extras(void)
{
    objects[2]=objects[3]=0;
    memset(solid_flags,0,sizeof solid_flags); memset(solid_ids,0,sizeof solid_ids);
    if (word(0xFFD8)) return; /* native VS remains exactly two competitors */
    for (unsigned p=2;p<s2_party.roster.slots;++p) {
        if (!native_id(p)) continue;
        unsigned o=0xCFC0;
        while (o>=0xB400 && g_ram[o]) o-=0x40;
        if (o<0xB400) { fprintf(stderr,"Sonic 2 party: no native object slot for P%u\n",p+1); exit(2); }
        objects[p]=o; respawn(o,p);
        unsigned k=kind(p);
        if (k<2 && !native_banks[k].count) {
            S2DonorLayout l=k==0?(S2DonorLayout){214,0x6FBE0,0x714E0,0x50000,0x29E2,8,0,0}:
                (S2DonorLayout){139,0x739E2,0x7446C,0x64320,0x29E2,8,0,0};
            char error[160];
            if (!s2_donor_decode(g_rom,0x100000,&l,&native_banks[k],error,sizeof error)) {
                fprintf(stderr,"Sonic 2 party: native character art: %s\n",error); exit(2);
            }
        }
    }
}
static uint32_t solid_single(uint32_t pc)
{
    switch (pc) {
    case 0x19718: return 0x19736;
    case 0x19778: return 0x1978E;
    case 0x197D0: return 0x197E6;
    case 0x19880: return 0x19896;
    case 0x19C32: return 0x19C48;
    case 0x19C8A: return 0x19CA0;
    case 0x19CE2: return 0x19CF8;
    default: return 0;
    }
}
static int player_object(unsigned at)
{
    for (unsigned p=0;p<4;++p) if (objects[p]==at) return 1;
    return 0;
}
static void solid_begin(unsigned p, uint8_t saved[144])
{
    for (unsigned n=0;n<144;++n) {
        unsigned at=0xB000+n*0x40;
        saved[n]=g_ram[at+0x22]&0x50;
        if (!player_object(at)) g_ram[at+0x22]=(g_ram[at+0x22]&~0x50)|solid_flags[p-2][n];
    }
}
static void solid_end(unsigned p, const uint8_t saved[144])
{
    for (unsigned n=0;n<144;++n) {
        unsigned at=0xB000+n*0x40;
        if (player_object(at)) continue;
        solid_flags[p-2][n]=g_ram[at+0x22]&0x50;
        g_ram[at+0x22]=(g_ram[at+0x22]&~0x50)|saved[n];
    }
}
static int collide_extras(uint32_t pc, uint32_t single)
{
    if (inside_solid || (!objects[2] && !objects[3])) return 0;
    M68KState input=g_cpu;
    inside_solid=1; recomp_call_addr(pc); inside_solid=0;
    M68KState result=g_cpu;
    /* Retain the original P1/P2 result registers: native spring/platform code
     * that reads them must not mistake an extra player for P1 or P2. */
    for (unsigned p=2;p<4;++p) {
        unsigned o=objects[p];
        if (!o || !g_ram[o] || g_ram[o+0x24]>=6) continue;
        uint8_t saved[144];
        solid_begin(p,saved);
        g_cpu=input; g_cpu.A[1]=0xFFFF0000u|o; g_cpu.D[6]=4;
        host_call(single);
        solid_end(p,saved);
    }
    g_cpu=result; return 1;
}
static int spring_extras(uint32_t pc)
{
    static int inside;
    if (inside || (!objects[2] && !objects[3])) return 0;
    M68KState input=g_cpu;
    unsigned spring=input.A[0]&0xFFFF;
    inside=1; recomp_call_addr(pc); inside=0;
    M68KState result=g_cpu;
    unsigned routine=g_ram[spring+0x24];
    if (g_ram[spring]!=0x41 || routine<2 || routine>10) return 1;
    for (unsigned p=2;p<4;++p) {
        unsigned o=objects[p]; if (!o || !g_ram[o] || g_ram[o+0x24]>=6) continue;
        uint8_t saved[144]; solid_begin(p,saved);
        g_cpu=input; g_cpu.A[1]=0xFFFF0000u|o; g_cpu.D[6]=4;
        g_cpu.D[1]=routine==4?0x13:0x1B;
        g_cpu.D[2]=routine==4?0xE:routine>=8?0x10:8;
        g_cpu.D[3]=routine==4?0xF:0x10; g_cpu.D[4]=word(spring+8);
        if (routine>=8) g_cpu.A[2]=routine==8?0x18FAA:0x18FC6;
        M68KState collision=native_helper(routine>=8?0x197E6:0x1978E);
        uint32_t response=0;
        if (routine==2 && (g_ram[spring+0x22]&0x10)) response=0x189CA;
        else if (routine==4 && (g_ram[spring+0x22]&0x40)) {
            unsigned facing=g_ram[spring+0x22]&1;
            if (word(spring+8)>=word(o+8)) facing^=1;
            if (!facing) response=0x18AEE;
        } else if (routine==6 && (int16_t)collision.D[4]==-2) response=0x18CC6;
        else if (routine==8 && (g_ram[spring+0x22]&0x10)) response=0x18DB4;
        else if (routine==10 && (int16_t)collision.D[4]==-2) response=0x18EE6;
        if (response) {
            g_cpu=input; g_cpu.A[1]=0xFFFF0000u|o; host_call(response);
            characters[p].special=0; /* a host spring takes over the trajectory */
        }
        solid_end(p,saved);
    }
    g_cpu=result; return 1;
}
static int update_player(unsigned p, uint32_t entry)
{
    unsigned object=g_cpu.A[0]&0xFFFF;
    if (inside_player) return 0;
    if (p && !word(0xFFD8)) {
        if (g_ram[0xB024]>=6) return 1;
        int dx=(int16_t)(word(object+8)-word(0xB008));
        int dy=(int16_t)(word(object+12)-word(0xB00C));
        if (dx < -480 || dx > 480 || dy < -320 || dy > 320) ++outside[p]; else outside[p]=0;
        if (g_ram[object+0x24]>=6 || outside[p]>120) respawn(object,p);
    }
    M68KState cpu=g_cpu;
    uint8_t inputs[12]; memcpy(inputs,g_ram+0xF600,sizeof inputs);
    uint16_t mode=word(0xFF70), bias=word(0xEED8);
    uint16_t record_index=word(0xEED2);
    uint8_t history[512]; memcpy(history,g_ram+0xE400,sizeof history);
    uint16_t logical2=word(0xF66A);
    uint8_t locked=g_ram[0xF7CC];
    uint8_t super=g_ram[0xFE19];
    uint8_t original_physics[6]; memcpy(original_physics,g_ram+0xF760,6);
    uint8_t original_dust[64]; memcpy(original_dust,g_ram+0xD100,64);
    uint8_t boundaries[8]; memcpy(boundaries,g_ram+0xEEC8,8);
    uint8_t object_control=g_ram[object+0x2A];
    int injected_control=0;
    /* Sonic's initialization writes P1 checkpoint records, even at Sidekick.
     * Keep those fields owned by the real P1 when a Sonic is a companion. */
    uint8_t checkpoint[16]; memcpy(checkpoint,g_ram+0xFE30,sizeof checkpoint);
    actor=(int)p; inside_player=1;
    if (p) g_ram[0xFE19]=0; /* Super Sonic's global state belongs only to P1. */
    if (p) memcpy(g_ram+0xF760,physics[p],6);
    if (p && word(0xFFD8)) memcpy(g_ram+0xEEC8,g_ram+0xEEF8,8);
    if (p) {
        uint8_t held=word(0xFFD8)?g_ram[0xF606]:companion_input(p,object);
        uint8_t pressed=(uint8_t)(held&~previous_input[p]); previous_input[p]=held;
        g_ram[0xF604]=g_ram[0xF602]=g_ram[0xF606]=g_ram[0xF66A]=held;
        g_ram[0xF605]=g_ram[0xF603]=g_ram[0xF607]=g_ram[0xF66B]=pressed;
        g_ram[0xF7CC]=g_ram[0xF7CD];
    }
    /* Native Tails already has a fully supported main-player path. */
    putword(0xFF70, p==0 && native_id(p)==2 ? 2 : 0);
    if (imported() && g_ram[object+0x24]==2) {
        S2Motion m=motion(object); S2Contacts c=contacts(object);
        s2_character_before(&characters[p],&m,g_ram[0xF604],g_ram[0xF605],&c);
        store_motion(object,&m);
        g_ram[0xF604]=g_ram[0xF602]=characters[p].native_held;
        g_ram[0xF605]=g_ram[0xF603]=characters[p].native_pressed;
        if (characters[p].move!=S2_MOVE_NATIVE) {
            if (characters[p].move!=S2_MOVE_FIXED) {
                host_call(0x163AC); /* ObjectMove, no gravity */
                host_call(characters[p].move==S2_MOVE_AIR?0x1AEAA:0x1E234);
            }
            host_call(0x1A974); /* LevelBound remains authoritative */
            g_ram[object+0x2A]|=1;
            injected_control=!(object_control&1);
        }
    }
    recomp_call_addr(entry);
    if (imported()) {
        if (injected_control) g_ram[object+0x2A]&=~1;
        memcpy(g_ram+0xF602,inputs+2,10);
    }
    inside_player=0; actor=-1;
    if (p>=2) g_ram[object+1]|=0x80;
    if (p) {
        /* Preserve Game_Mode changes, but not companion copies of pad state. */
        g_ram[0xFE19]=super;
        memcpy(g_ram+0xF602,inputs+2,10); putword(0xF66A,logical2);
        putword(0xEED8,bias); g_ram[0xF7CC]=locked;
        memcpy(g_ram+0xFE30,checkpoint,sizeof checkpoint);
        memcpy(g_ram+0xE400,history,sizeof history); putword(0xEED2,record_index);
        memcpy(physics[p],g_ram+0xF760,6); memcpy(g_ram+0xF760,original_physics,6);
        memcpy(g_ram+0xD100,original_dust,64);
        memcpy(g_ram+0xEEC8,boundaries,8);
    }
    putword(0xFF70,mode);
    g_cpu=cpu;
    return 1;
}
static int special_hook(uint32_t pc)
{
    if (pc==0x5152) {
        objects[2]=objects[3]=0; /* SS reserves a different native object pool. */
        g_ram[0xB000]=native_id(0)==2?0x10:9;
        g_ram[0xB040]=native_id(1)?(native_id(1)==2?0x10:9):0;
        return 0;
    }
    if (pc==0x338EC || pc==0x347EC) {
        unsigned o=g_cpu.A[0]&0xFFFF;
        if (inside_player || (o!=0xB000 && o!=0xB040)) return 0;
        unsigned p=o==0xB040; int init=!g_ram[o+0x24];
        M68KState before=g_cpu;
        uint16_t mode=word(0xFF70), controls=word(0xF602);
        actor=p; inside_player=1;
        /* Role and character remain separate in the native half-pipe rules. */
        putword(0xFF70,p==0 && pc==0x347EC?2:mode);
        if (p && pc==0x338EC) {
            if (!input_player_connected(1) && !input_script_player_used(1)) host_call(0x34972);
            putword(0xF602,word(0xF66A));
        }
        recomp_call_addr(pc);
        if (init) { putword(o+0x34,p?0x80:0x6E); g_ram[o+0x18]=p?2:3; }
        putword(0xFF70,mode); putword(0xF602,controls);
        inside_player=0; actor=-1; g_cpu=before; return 1;
    }
    if (!inside_player) return 0;
    if (pc==0x33AB2 && imported()) {
        unsigned o=g_cpu.A[0]&0xFFFF;
        if (g_ram[o+0x23]) --g_ram[o+0x23];
        return 1; /* don't overwrite a real Sonic's shared SS DMA tiles */
    }
    if (pc==0x3399E || pc==0x34888) {
        /* Each ROLE owns a distinct shadow, including two Obj09-based imports. */
        g_cpu.A[1]=actor?0xFFFFB180u:0xFFFFB140u; return 0;
    }
    if (pc==0x34972)
        return actor==0 || input_player_connected(1) || input_script_player_used(1);
    if (pc==0x33E44 && actor==0 && kind(0)==S2_CHAR_TAILS) {
        static int swapping;
        if (swapping) return 0;
        uint16_t mode=word(0xFF70); putword(0xFF70,native_id(1)?0:1);
        swapping=1; recomp_call_addr(pc); swapping=0; putword(0xFF70,mode); return 1;
    }
    return pc==0x164F4 && imported();
}
int s2_runtime_hook(uint32_t pc)
{
#if GENESIS_HAS_RECOMP_NET
    if (genesis_netplay_active()) return 0;
#endif
    if (pc==0x4F64) {
        active=!vanilla();
        if (active) putword(0xFF70,native_id(1)?0:1);
        return 0;
    }
    if (special_stage()) return special_hook(pc);
    if (pc==0x4450) { /* Level_SetPlayerMode: attract demos remain byte-identical */
        active=g_ram[0xF600]!=0x88 && !vanilla();
        if (!active) return 0;
        s2_roster_validate(&s2_party.roster);
        memset(previous_input,0,sizeof previous_input); memset(outside,0,sizeof outside);
        for (unsigned p=0;p<4;++p) {
            s2_character_reset(&characters[p],kind(p));
            const uint8_t defaults[]={6,0,0,12,0,128}; memcpy(physics[p],defaults,6);
        }
        putword(0xFF70,native_id(0)==2?2:native_id(1)?0:1);
        return 1;
    }
    if (!level()) return 0;
    if ((pc==0x1296A || pc==0x129E0 || pc==0x12A2C) && !word(0xFFD8)) {
        static int reward;
        if (reward) return 0;
        unsigned target=g_cpu.A[1]&0xFFFF, p=0;
        while (p<4 && objects[p]!=target) ++p;
        if (p==4 || !p) return 0;
        uint32_t parent=g_cpu.A[1]; uint8_t tails_physics[6], shield[64];
        memcpy(tails_physics,g_ram+0xFEC0,6); memcpy(shield,g_ram+0xD1C0,64);
        if (pc==0x1296A) g_cpu.A[1]=0xFFFFB000u; /* shared campaign ring/life pool */
        reward=1; recomp_call_addr(pc); reward=0; g_cpu.A[1]=parent;
        if (pc==0x129E0 && native_id(p)!=2) {
            memcpy(physics[p],g_ram+0xFEC0,6); memcpy(g_ram+0xFEC0,tails_physics,6);
        }
        if (pc==0x12A2C && p>=2) {
            unsigned free_slot=0;
            for (unsigned at=0xB400;at<0xD000;at+=64) {
                if (g_ram[at]==0x38 && word(at+0x3E)==target) { free_slot=at; break; }
                if (!g_ram[at] && !free_slot) free_slot=at;
            }
            if (free_slot) memcpy(g_ram+free_slot,g_ram+0xD1C0,64);
            memcpy(g_ram+0xD1C0,shield,64);
        }
        return 1;
    }
    if (pc==0x15F9C) {
        for (unsigned n=0;n<144;++n) {
            unsigned id=g_ram[0xB000+n*0x40];
            if (id!=solid_ids[n]) { solid_flags[0][n]=solid_flags[1][n]=0; solid_ids[n]=(uint8_t)id; }
        }
        return 0;
    }
    uint32_t single=solid_single(pc);
    if (single) return collide_extras(pc,single);
    if (pc==0x18888) return spring_extras(pc);
    if (pc==0x189CA || pc==0x18AEE || pc==0x18CC6 || pc==0x18DB4 || pc==0x18EE6) {
        for (unsigned p=0;p<4;++p) if (objects[p] && objects[p]==(g_cpu.A[1]&0xFFFF))
            characters[p].special=0;
        return 0;
    }
    if (pc==0x446E) { /* InitPlayers: host role is distinct from character ID */
        g_ram[0xB000]=(uint8_t)native_id(0); g_ram[0xD100]=8;
        unsigned p2=s2_party.roster.slots>1?native_id(1):0;
        g_ram[0xB040]=(uint8_t)p2;
        if (p2) {
            putword(0xB048,word(0xB008)-32); putword(0xB04C,word(0xB00C)+4);
            g_ram[0xD140]=8;
        }
        if (native_id(0)==2) putword(0xB00C,word(0xB00C)+4);
        reserve_extras();
        return 1;
    }
    if (pc==0x19F50 || pc==0x1B8A4) {
        unsigned address=g_cpu.A[0]&0xFFFF;
        for (unsigned p=0;p<4;++p) if (objects[p] && address==objects[p]) return update_player(p,pc);
        return 0;
    }
    if (pc==0x1BAD4) return inside_player && actor>0; /* character-independent companion control */
    if (pc==0x1ABA6 || pc==0x1AB38)
        return inside_player && (actor>0 || imported()); /* P1 Sonic progression only */
    if (pc==0x164F4 && inside_player && actor>=2) return 1;
    if (pc==0x3F73C && inside_player) {
        unsigned o=g_cpu.A[0]&0xFFFF, monitor=g_cpu.A[1]&0xFFFF;
        unsigned animation=characters[actor].animation;
        int hammer=imported() && kind(actor)==S2_CHAR_AMY && animation>=0x23 && animation<=0x25;
        if (hammer || (actor>0 && !word(0xFFD8) && (int16_t)word(o+0x12)>=0)) {
            /* Stock single-player companions cannot break monitors. Local
             * party actors can, retaining a stable native parent for rewards.
             * Amy $41B78/$41C14 also permits an upward-moving hammer strike. */
            if (hammer || g_ram[o+0x1C]==2) {
                if ((int16_t)word(o+0x12)>0) putword(o+0x12,-(int16_t)word(o+0x12));
                g_ram[monitor+0x24]=4; putword(monitor+0x3E,o);
            }
            return 1;
        }
    }
    if (!inside_player || !imported()) return 0;
    unsigned o=g_cpu.A[0]&0xFFFF;
    if ((pc==0x3F5A0 || pc==0x3F69C) && kind(actor)==S2_CHAR_AMY && characters[actor].animation==0x23 &&
        (g_cpu.A[1]&0xFFFF)==0xB400) {
        /* Amy donor TouchResponse $41964: grounded hammer's forward 34x50 box. */
        g_cpu.D[2]=(uint16_t)(word(o+8)-((g_ram[o+0x22]&1)?25:9));
        g_cpu.D[3]=(uint16_t)(word(o+12)-26); g_cpu.D[4]=34; g_cpu.D[5]=50;
        return 0;
    }
    if (pc==0x164F4 || pc==0x1B848) return 1; /* donor art is host-rendered, not native DMA/SAT */
    if (pc==0x1AC3E && kind(actor)==S2_CHAR_AMY) return 1;
    if (pc==0x1B350) {
        S2Motion m=motion(o); S2Contacts c=contacts(o);
        s2_character_after(&characters[actor],&m,&c);
        s2_character_animate(&characters[actor],&m,bank(actor)); store_motion(o,&m);
        g_ram[o+1]=(g_ram[o+1]&~3)|characters[actor].render_flip|0x80;
        return 1;
    }
    if (pc==0x3F554) {
        /* Native combat outcomes, actor-specific attack predicate. Donor Amy
         * deliberately cannot damage enemies with her ordinary unarmed jump. */
        S2Motion m=motion(o); uint8_t animation=g_ram[o+0x1C];
        g_ram[o+0x1C]=s2_character_attacking(&characters[actor],&m)?2:0;
        static int combat;
        if (combat) { g_ram[o+0x1C]=animation; return 0; }
        combat=1; recomp_call_addr(pc); combat=0; g_ram[o+0x1C]=animation;
        return 1;
    }
    return 0;
}
static void special_overlay(const GVDP *v, int line, uint32_t *out, int width)
{
    /* Half-pipe collision, rings, jumps, bombs and progress stay native. The
     * imports use a rotated/perspective-scaled gameplay-art adapter, not a
     * replacement special stage or assets from either donor's title screen. */
    if (!(v->reg[1]&64) || (v->reg[12]&1)) return; /* exclude the results screen */
    for (int p=1;p>=0;--p) {
        unsigned o=objects[p];
        if (kind(p)<S2_CHAR_AMY || g_ram[o]!=9 || !g_ram[o+0x24] || (g_ram[o+0x23]&1)) continue;
        const S2DonorBank *b=bank(p); if (!b) continue;
        unsigned anim=g_ram[o+0x24]==4?2:1;
        unsigned count=anim==2?4:4, frame=b->animations[anim][1+(word(0xFE0E)/3)%count];
        if (frame>=b->count) continue;
        const S2DonorFrame *f=&b->frames[frame];
        int angle=(g_ram[o+0x26]-64)&255, sn=sine(angle), cs=sine(angle+64);
        int x=word(o+8)+(width-256)/2,y=word(o+12);
        int scale=word(o+0x34)>=0x77?384:352; /* nearer player is larger */
        int dy=(line-y)*256/scale;
        for (int dx=-64;dx<64;++dx) {
            int dest=x+dx; if (dest<0 || dest>=width) continue;
            int sx=dx*256/scale;
            int fx=(sx*cs+dy*sn)/256-f->x,fy=(-sx*sn+dy*cs)/256-f->y;
            if (fx<0 || fy<0 || fx>=f->width || fy>=f->height) continue;
            unsigned color=f->pixels[fy*f->width+fx];
            if (color) out[dest]=genesis_dac_cram_to_argb(b->palette[color],GENESIS_DAC_NORMAL);
        }
    }
}
void s2_runtime_overlay(const GVDP *v, int line, uint32_t *out, int width)
{
    if (special_stage()) { special_overlay(v,line,out,width); return; }
    if (!level() || !g_ram[0xF711] || !(v->reg[1]&64)) return;
    int left,top;
    if (word(0xFFD8)) {
        /* Native interlace pass is 448 rows. Each competitor's 224-row world
         * view contains both characters, with the stock central SAT mask. */
        if (line>=216 && line<248) return;
        unsigned camera=line<224?0xEE60:0xEE80;
        left=word(camera)-(width-320)/2;
        top=(int16_t)word(camera+4)-(line<224?0:224);
    } else s2_video_actor_origin(v,line,width,&left,&top);
    unsigned fade=0;
    for (unsigned i=0;i<16;++i) {
        unsigned color=v->cram[i]; unsigned value=(color&14)+((color>>4)&14)+((color>>8)&14);
        if (value>fade) fade=value;
    }
    if (!fade) return;
    for (int p=3;p>=0;--p) {
        unsigned o=objects[p];
        if (!o || (p<2 && kind(p)<S2_CHAR_AMY) || !g_ram[o] || (word(o+0x30) && !(word(o+0x30)&8))) continue;
        const S2DonorBank *b=bank(p); unsigned frame=g_ram[o+0x1A];
        if (!b || frame>=b->count) continue;
        const S2DonorFrame *f=&b->frames[frame];
        int x=(int16_t)word(o+8)-left,y=(int16_t)word(o+12)-top;
        unsigned flip=kind(p)<S2_CHAR_AMY?g_ram[o+1]&3:characters[p].render_flip;
        int fy=line-y-(flip&2?-f->y-f->height:f->y);
        if (fy<0 || fy>=f->height) continue;
        if (flip&2) fy=f->height-1-fy;
        int x0=x+(flip&1?-f->x-f->width:f->x);
        for (unsigned px=0;px<f->width;++px) {
            int dx=x0+px; if (dx<0 || dx>=width) continue;
            unsigned color=f->pixels[fy*f->width+(flip&1?f->width-1-px:px)];
            if (!color || !s2_video_actor_pixel_visible(v,dx+left,line+top,word(o+2)&0x8000)) continue;
            uint32_t rgb=genesis_dac_cram_to_argb(kind(p)<S2_CHAR_AMY?v->cram[color]:b->palette[color],GENESIS_DAC_NORMAL);
            if (fade<42) rgb=0xFF000000|((((rgb>>16)&255)*fade/42)<<16)|((((rgb>>8)&255)*fade/42)<<8)|((rgb&255)*fade/42);
            out[dx]=rgb;
        }
    }
}
