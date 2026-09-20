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
/* The native character decides whether to submit a sprite, including hurt
 * flashing. Reuse that decision instead of approximating its timer phase. */
static uint8_t displayed[4];
/* Tails_control_counter through Tails_CPU_jumping ($F702..$F70F).
 * Each companion runs the original P2 AI with its own native state. */
static uint8_t companion_cpu[4][14];
static int inside_companion_cpu;
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
typedef struct {
    int16_t x,y;
    uint16_t art;
    uint8_t frame,flip,visible;
} PartySprite;
static PartySprite published_sprites[4];
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
    gvdp_set_unlimited_sprites(0);
    memset(published_sprites,0,sizeof published_sprites);
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
    if (s2_party.save_menu_enabled) return "Campaign save sessions are not serialized by machine quickstates; load a campaign file from Data Select.";
    return vanilla()?NULL:"Experimental party controllers are not serialized by machine quickstates; use a fresh native game.";
}
static int level(void) { return active && ((g_ram[0xF600]&0x7F)==12); }
static unsigned native_id(unsigned p)
{
    const S2Character *c=s2_character_find(s2_party.roster.character[p]);
    return c && c->available() ? c->native_object_id : 0;
}
static int human_companion(unsigned p)
{
    return input_player_connected(p) || input_script_player_used(p);
}
static int companion_recovering(void)
{
    unsigned state=word(0xF708);
    return state==2 || state==4;
}
static void companion_control(unsigned p, unsigned o)
{
    if (g_ram[o+0x24]!=2) return;
    /* Host-rendered actors never go through BuildSprites' on-screen writer.
     * Supply its camera-relative visibility instead of forcing bit 7 on. */
    int x=(int16_t)(word(o+8)-word(0xEE00));
    int y=(int16_t)(word(o+12)-word(0xEE04));
    int visible=x>=-(int)g_ram[o+0x19] && x<320+g_ram[o+0x19] &&
        y>=-32 && y<256;
    g_ram[o+1]=(g_ram[o+1]&0x7F)|(visible?0x80:0);
    int human=human_companion(p);
    /* Preserve the party's explicit controller ownership; the CPU must not
     * take over an idle connected controller after the stock ten seconds. */
    if (human) putword(0xF702,600);
    uint16_t index=word(0xEED2),frame=word(0xFE04),record_x=0;
    unsigned record=0;
    int varied=p>=2 && !human && word(0xF708)==6 && !g_ram[o+0x2A];
    if (varied) {
        /* Keep P2 authentic. P3/P4 react seven/thirteen frames later and use
         * separate native jump-retry phases. Only normal following varies:
         * waiting, flying, and the safe re-entry target stay native. */
        unsigned delayed=(index&0xFF00)|((index-4*(p==2?7:13))&255);
        putword(0xEED2,delayed);
        putword(0xFE04,frame+(p==2?13:37));
        unsigned at=(delayed-0x44)&255;
        unsigned status=g_ram[0xE402+at];
        if (!(status&0xDA)) { /* grounded terrain, not an object/water/respawn lock */
            unsigned gap=(p==2?28:72)+(((frame>>8)*13+p*7)&7);
            unsigned address=0xE500+at;
            int target=word(address)+((status&1)?(int)gap:-(int)gap);
            if (target>=word(0xEEC8)+16 && target<=word(0xEECA)+288) {
                record=address; record_x=word(record); putword(record,target);
            }
        }
    }
    inside_companion_cpu=1;
    native_helper(0x1BAD4); /* TailsCPU_Control: delayed P1 history, safe flight */
    inside_companion_cpu=0;
    if (varied) {
        if (record) putword(record,record_x);
        putword(0xEED2,index); putword(0xFE04,frame);
        /* Briefly coast at distinct, repeatable intervals. Never discard a
         * brake, jump, airborne steering, or input during object control. */
        unsigned direction=g_ram[0xF66A]&12;
        int inertia=(int16_t)word(o+0x14);
        int driving=(direction==8 && inertia>0)||(direction==4 && inertia<0);
        unsigned phase=(frame+(p==2?7:19))&31;
        if (word(0xF708)==6 && !g_ram[o+0x2A] && !(g_ram[o+0x22]&0x2E) &&
            !g_ram[o+0x39] && driving && phase<(p==2?1u:2u)) {
            g_ram[0xF66A]&=~12; g_ram[0xF66B]&=~12;
        }
    }
    if (!human) {
        /* CPU ABC means an ordinary jump. Amy's A is a hammer and Knuckles'
         * repeated airborne jump is a glide, so never synthesize those moves. */
        for (unsigned at=0xF66A;at<=0xF66B;++at)
            g_ram[at]=(g_ram[at]&~0x70)|((g_ram[at]&0x70)?0x10:0);
        if (kind(p)==S2_CHAR_KNUCKLES && (g_ram[o+0x22]&2)) g_ram[0xF66B]&=~0x10;
    }
    if (companion_recovering()) {
        characters[p].special=0;
        if (native_id(p)!=2) g_ram[o+0x1C]=2; /* use this character's roll art */
        if (p>=2) memset(solid_flags[p-2],0,sizeof solid_flags[p-2]);
    }
}
static void spawn_extra(unsigned object, unsigned p)
{
    /* Level start only. Recovery retains the object and uses TailsCPU_Despawn;
     * never reinitialize a live actor at an arbitrary offset inside terrain. */
    memset(g_ram+object,0,0x40);
    g_ram[object]=(uint8_t)native_id(p);
    putword(object+8,word(0xB008));
    putword(object+12,word(0xB00C));
    g_ram[object+0x22]=2;
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
        objects[p]=o; spawn_extra(o,p);
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
    int companion=p && !word(0xFFD8);
    int initializing=!g_ram[object+0x24];
    displayed[p]=0;
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
    uint8_t saved_cpu[14]; memcpy(saved_cpu,g_ram+0xF702,sizeof saved_cpu);
    if (companion) memcpy(g_ram+0xF702,companion_cpu[p],sizeof saved_cpu);
    actor=(int)p; inside_player=1;
    if (p) g_ram[0xFE19]=0; /* Super Sonic's global state belongs only to P1. */
    if (p) memcpy(g_ram+0xF760,physics[p],6);
    if (p && word(0xFFD8)) memcpy(g_ram+0xEEC8,g_ram+0xEEF8,8);
    if (p) {
        uint8_t held=word(0xFFD8)?g_ram[0xF606]:
            (uint8_t)(input_current_mask(p)|input_script_player_mask(p));
        uint8_t pressed=(uint8_t)(held&~previous_input[p]); previous_input[p]=held;
        g_ram[0xF604]=g_ram[0xF602]=g_ram[0xF606]=g_ram[0xF66A]=held;
        g_ram[0xF605]=g_ram[0xF603]=g_ram[0xF607]=g_ram[0xF66B]=pressed;
        g_ram[0xF7CC]=g_ram[0xF7CD];
        if (companion) {
            companion_control(p,object);
            g_ram[0xF604]=g_ram[0xF602]=g_ram[0xF606]=g_ram[0xF66A];
            g_ram[0xF605]=g_ram[0xF603]=g_ram[0xF607]=g_ram[0xF66B];
        }
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
    if (!p && native_id(0)==2) {
        /* Native Tails writes his own position ring. As leader he must also
         * publish the Sonic history consumed by every companion's P2 AI. */
        if (initializing) {
            memset(g_ram+0xE400,0,256);
            for (unsigned at=0;at<256;at+=4) {
                putword(0xE500+at,word(object+8)); putword(0xE502+at,word(object+12));
                g_ram[0xE402+at]=g_ram[object+0x22];
            }
            putword(0xEED2,0);
        }
        native_helper(0x1A15C);
    }
    if (imported()) {
        if (injected_control) g_ram[object+0x2A]&=~1;
        memcpy(g_ram+0xF602,inputs+2,10);
    }
    inside_player=0; actor=-1;
    if (companion) {
        memcpy(companion_cpu[p],g_ram+0xF702,sizeof saved_cpu);
        /* World objects referring to native P2 still see P2's AI state. */
        if (p!=1) memcpy(g_ram+0xF702,saved_cpu,sizeof saved_cpu);
    }
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
int s2_runtime_hook(uint32_t pc)
{
#if GENESIS_HAS_RECOMP_NET
    if (genesis_netplay_active()) return 0;
#endif
    if (pc==0x4F64) {
        /* Special stages ALWAYS use stock Sonic + Tails, even for a solo
         * campaign roster. Native controllers, art, shadows and CPU follow
         * run unmodified. Keep the persisted roster untouched: level init
         * restores it after the native special-stage return. */
        active=inside_player=0; actor=-1;
        gvdp_set_unlimited_sprites(0);
        objects[2]=objects[3]=0;
        putword(0xFF70,0);
        return 0;
    }
    if (pc==0x4450) { /* Level_SetPlayerMode: attract demos remain byte-identical */
        active=g_ram[0xF600]!=0x88 && !vanilla();
        gvdp_set_unlimited_sprites(active);
        memset(published_sprites,0,sizeof published_sprites);
        if (!active) return 0;
        s2_roster_validate(&s2_party.roster);
        memset(previous_input,0,sizeof previous_input); memset(companion_cpu,0,sizeof companion_cpu);
        memset(displayed,0,sizeof displayed);
        for (unsigned p=0;p<4;++p) {
            s2_character_reset(&characters[p],kind(p));
            const uint8_t defaults[]={6,0,0,12,0,128}; memcpy(physics[p],defaults,6);
        }
        putword(0xFF70,native_id(0)==2?2:native_id(1)?0:1);
        return 1;
    }
    if (!level()) return 0;
    if ((pc==0x19FE6 || pc==0x1B96E) && inside_player && actor>0 && !word(0xFFD8)) {
        /* Obj01/02_Init_Continued, before the first movement/collision query.
         * Obj01 skips art/plane initialization at checkpoints; Obj79_LoadData
         * restores only MainCharacter. Every companion spawns in P1's current
         * plane, including catch-up on a secondary layer with no checkpoint.
         * Doing this at the init tail also avoids native fresh-start defaults
         * overwriting a plane that was seeded before Obj01_Init. */
        unsigned o=g_cpu.A[0]&0xFFFF;
        putword(o+0x3E,word(0xB03E));
        putword(o+2,(native_id((unsigned)actor)==2?0x7A0:0x780)|(word(0xB002)&0x8000));
        return 0;
    }
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
    if (pc==0x1BAD4) return inside_player && actor>0 && !inside_companion_cpu;
    if ((pc==0x1B21C || pc==0x1CC6C) && inside_player && actor>0 && !word(0xFFD8)) {
        /* Obj02_CheckGameOver waits for the corpse to leave the level, then
         * enters TailsCPU_Despawn without spending P1's lives or locking P1's
         * camera. Apply it to Sonic/Amy/Knuckles companions as well. */
        static int checking_death;
        if (checking_death) return 0;
        uint16_t max_y=word(0xEEFE);
        uint8_t scroll_lock=g_ram[0xEEBF];
        putword(0xEEFE,word(0xEECE));
        checking_death=1; native_helper(0x1CC6C); checking_death=0;
        putword(0xEEFE,max_y); g_ram[0xEEBF]=scroll_lock;
        if (companion_recovering() && native_id((unsigned)actor)!=2) g_ram[(g_cpu.A[0]&0xFFFF)+0x1C]=2;
        return 1;
    }
    if (pc==0x1ABA6 || pc==0x1AB38)
        return inside_player && (actor>0 || imported()); /* P1 Sonic progression only */
    if (pc==0x164F4 && inside_player) {
        if ((g_cpu.A[0]&0xFFFF)==objects[actor]) {
            if (actor>0 && !word(0xFFD8) && word(0xF708)==2) return 1;
            displayed[actor]=1;
        }
        if (actor>=2 || imported()) return 1;
    }
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
    if (pc==0x1B848) return 1; /* donor art is host-rendered, not native DMA/SAT */
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
void s2_runtime_capture(void)
{
    /* BuildSprites runs after the complete object tick. Rendering live actor
     * RAM per scanline could use one pose for a head and the next for its body,
     * or observe displayed[] while an actor was being updated. */
    memset(published_sprites,0,sizeof published_sprites);
    if (!level()) return;
    for (unsigned p=0;p<4;++p) {
        unsigned o=objects[p];
        if (!o || !g_ram[o] || (p<2 && kind(p)<S2_CHAR_AMY)) continue;
        PartySprite *s=&published_sprites[p];
        s->x=(int16_t)word(o+8); s->y=(int16_t)word(o+12); s->art=word(o+2);
        s->frame=g_ram[o+0x1A]; s->visible=displayed[p];
        s->flip=kind(p)<S2_CHAR_AMY?g_ram[o+1]&3:characters[p].render_flip;
    }
}
void s2_runtime_overlay(const GVDP *v, int line, uint32_t *out, int width)
{
    static PartySprite frame_sprites[4];
    static GVDP frame_vdp;
    static uint8_t frame_world[65536];
    static int frame_active,frame_vs,left,top,vs_left,vs_top;
    if (!line) {
        frame_active=level() && g_ram[0xF711];
        memcpy(frame_sprites,published_sprites,sizeof frame_sprites);
        frame_vdp=*v; memcpy(frame_world,g_ram,sizeof frame_world);
        frame_vs=word(0xFFD8);
        s2_video_actor_origin(&frame_vdp,0,width,&left,&top);
        if (frame_vs) {
            left=word(0xEE60)-(width-320)/2; top=(int16_t)word(0xEE64);
            vs_left=word(0xEE80)-(width-320)/2; vs_top=(int16_t)word(0xEE84)-224;
        }
    }
    if (!frame_active || !(v->reg[1]&64)) return;
    int origin_x=left,origin_y=top;
    if (frame_vs) {
        /* Native interlace pass is 448 rows. Each competitor's 224-row world
         * view contains both characters, with the stock central SAT mask. */
        if (line>=216 && line<248) return;
        if (line>=224) { origin_x=vs_left; origin_y=vs_top; }
    }
    unsigned fade=0;
    for (unsigned i=0;i<16;++i) {
        unsigned color=v->cram[i]; unsigned value=(color&14)+((color>>4)&14)+((color>>8)&14);
        if (value>fade) fade=value;
    }
    if (!fade) return;
    for (int p=3;p>=0;--p) {
        const PartySprite *s=&frame_sprites[p];
        if (!s->visible) continue;
        const S2DonorBank *b=bank(p); unsigned frame=s->frame;
        if (!b || frame>=b->count) continue;
        const S2DonorFrame *f=&b->frames[frame];
        int x=s->x-origin_x,y=s->y-origin_y;
        unsigned flip=s->flip;
        int fy=line-y-(flip&2?-f->y-f->height:f->y);
        if (fy<0 || fy>=f->height) continue;
        if (flip&2) fy=f->height-1-fy;
        int x0=x+(flip&1?-f->x-f->width:f->x);
        for (unsigned px=0;px<f->width;++px) {
            int dx=x0+px; if (dx<0 || dx>=width) continue;
            unsigned color=f->pixels[fy*f->width+(flip&1?f->width-1-px:px)];
            if (!color || !s2_video_actor_pixel_visible(&frame_vdp,dx+origin_x,line+origin_y,s->art&0x8000,frame_world)) continue;
            uint32_t rgb=genesis_dac_cram_to_argb(kind(p)<S2_CHAR_AMY?v->cram[color]:b->palette[color],GENESIS_DAC_NORMAL);
            if (fade<42) rgb=0xFF000000|((((rgb>>16)&255)*fade/42)<<16)|((((rgb>>8)&255)*fade/42)<<8)|((rgb&255)*fade/42);
            out[dx]=rgb;
        }
    }
}
