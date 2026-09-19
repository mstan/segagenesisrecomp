/* Behavior port references: docs/SONIC2_DONOR_PORT.md. All host-world
 * consequences are handled by the Sonic 2 adapter, not by these controllers. */
#include "sonic2_character.h"
#include <string.h>
enum { UP=1, DOWN=2, LEFT=4, RIGHT=8, BC=0x30, A=0x40, JUMP=0x70 };
enum { FACE=1, AIR=2, ROLL=4, ON_OBJECT=8, ROLLJUMP=16, PUSH=32, WATER=64 };
enum { AMY_NORMAL, AMY_HAMMER, AMY_AIR_HAMMER, AMY_WHIRL, AMY_HAMMER_JUMP,
       AMY_GIANT, AMY_GIANT_ROLL, AMY_DASH };
static int absword(int value) { return value<0?-value:value; }
static int direction(const S2Motion *m) { return m->status&FACE?-1:1; }
void s2_character_reset(S2CharacterState *s, unsigned kind)
{ memset(s,0,sizeof *s); s->kind=kind; s->animation=~0u; }
static void amy_before(S2CharacterState *s, S2Motion *m)
{
    s->native_held&=~A; s->native_pressed&=~A;
    if (!(m->status&AIR)) {
        if (s->special==AMY_DASH) {
            s->move=S2_MOVE_FIXED; s->native_held=s->native_pressed=0;
            if (s->held&UP) {
                s->charge-=s->charge>>5;
                if (s->pressed&BC) { s->charge+=0x200; if (s->charge>0x800) s->charge=0x800; }
                m->animation=1;
            } else {
                /* Donor $1BE44: $800,$880,...,$C00, indexed by charge high byte. */
                m->inertia=(int16_t)(direction(m)*(0x800+(s->charge>>8)*0x80));
                s->special=AMY_NORMAL; m->animation=0;
            }
            return;
        }
        if ((s->held&UP) && (s->pressed&BC) && !m->inertia) {
            s->special=AMY_DASH; s->charge=0; s->move=S2_MOVE_FIXED;
            s->native_pressed=0; m->animation=1; return;
        }
        if ((s->held&DOWN) && (s->pressed&A)) {
            s->special=AMY_HAMMER_JUMP; s->age=0;
            s->native_pressed=0x20; s->native_held|=0x20;
            s->jump_adjustment=-0x250; /* donor $1B998 + ordinary $680 jump */
        } else if ((s->held&DOWN) && (s->pressed&BC)) {
            s->special=AMY_GIANT; s->age=0;
            m->vx=(int16_t)(direction(m)*((m->status&WATER)?0x700:m->boosted?0xC00:0x900));
            m->vy=(m->status&WATER)?0x280:0x380;
        } else if (!(s->held&DOWN) && (s->pressed&A)) {
            s->special=AMY_HAMMER; s->age=0; m->animation=0x28;
        }
        if (s->special==AMY_HAMMER) {
            s->native_held=s->native_pressed=0;
            /* $1BA06: lose one eighth of inertia during the hammer swing. */
            m->inertia=(int16_t)(m->inertia-(m->inertia>>3));
        }
    } else {
        if (s->special==AMY_GIANT && (s->pressed&A)) {
            s->special=AMY_GIANT_ROLL; s->age=0;
        } else if (s->special!=AMY_GIANT_ROLL && s->special!=AMY_GIANT && (s->pressed&A)) {
            s->age=0;
            if (s->held&DOWN) {
                s->special=AMY_WHIRL; m->vx=0; if (m->vy<0) m->vy=0;
            } else s->special=AMY_AIR_HAMMER;
        }
        if (s->special==AMY_GIANT_ROLL) s->native_held=s->native_pressed=0;
    }
    /* Amy's hack does not bind a down-held native Sonic spindash. */
    if (s->special==AMY_GIANT || s->special==AMY_HAMMER_JUMP) s->native_held&=~DOWN;
}
static void knuckles_before(S2CharacterState *s, S2Motion *m, const S2Contacts *c)
{
    if (!(m->status&AIR) && s->special!=S2_SK_SLIDE && s->special!=S2_SK_LEDGE) s->special=S2_SK_NORMAL;
    s->jump_adjustment=(m->status&WATER)?0:0x80; /* stock normal Knuckles jump $600 */
    if (s->special==S2_SK_NORMAL && (m->status&AIR) && m->jumping &&
        m->vy>=((m->status&WATER)?-0x200:-0x400) && (s->pressed&JUMP)) {
        s->special=S2_SK_GLIDE; s->turn=(m->status&FACE)?0x80:0;
        m->radius_x=m->radius_y=10; m->status&=~(ROLL|ROLLJUMP);
        m->inertia=0x400; m->vx=(int16_t)(direction(m)*0x400);
        m->vy+=0x200; if (m->vy<0) m->vy=0; m->angle=0;
    }
    switch (s->special) {
    case S2_SK_GLIDE: {
        s->move=S2_MOVE_AIR;
        if (!(s->held&JUMP)) {
            s->special=S2_SK_FALL; m->vx>>=2; m->radius_x=9; m->radius_y=19;
            m->animation=0x21; break;
        }
        int speed=(uint16_t)m->inertia;
        if (speed<0x400) speed+=8; else if (speed<0x1800 && !(s->turn&0x7F)) speed+=4;
        m->inertia=(int16_t)speed;
        int turn=(int8_t)s->turn;
        if ((s->held&LEFT) && s->turn!=0x80) { if (turn<0) turn=-turn; turn+=2; }
        else if ((s->held&RIGHT) && s->turn) { if (turn>=0) turn=-turn; turn+=2; }
        else if (s->turn&0x7F) turn+=2;
        s->turn=(uint8_t)turn;
        m->vx=(int16_t)((c->sine((s->turn+0x40)&255)*speed)>>8);
        m->vy+=(m->vy<0x80)?0x20:-0x20;
        m->status=(m->status&~FACE)|((s->turn==0x80)?FACE:0);
        m->animation=0x20;
        break;
    }
    case S2_SK_FALL:
        m->animation=0x21; break; /* native airborne steering/gravity */
    case S2_SK_SLIDE:
        s->move=S2_MOVE_GROUND;
        if (!(s->held&JUMP) || absword(m->vx)<=0x20 || c->floor>=14) {
            if (c->floor>=14) { s->special=S2_SK_FALL; m->status|=AIR; }
            else { s->special=S2_SK_NORMAL; m->vx=m->vy=m->inertia=0; m->animation=0x22; }
            m->radius_x=9; m->radius_y=19;
        } else { m->vx+=(m->vx<0)?0x20:-0x20; m->inertia=m->vx; }
        break;
    case S2_SK_CLIMB:
        s->move=S2_MOVE_FIXED; m->vx=m->vy=m->inertia=0;
        if (m->x!=s->wall_x || (m->status&ON_OBJECT) || c->wall<0) { s->special=S2_SK_FALL; break; }
        if (s->pressed&JUMP) {
            m->status^=FACE; m->status|=AIR|ROLL; m->jumping=1;
            m->vx=(int16_t)(direction(m)*0x400); m->vy=-0x380;
            m->radius_y=14; m->radius_x=7; m->animation=2; s->special=S2_SK_NORMAL;
        } else if ((s->held&UP) && c->wall>=4) { s->special=S2_SK_LEDGE; s->ledge=0; s->age=0; }
        else if ((s->held&UP) && !c->wall && c->ceiling>=0) m->y-=65536;
        else if (s->held&DOWN) {
            if (c->wall>0) s->special=S2_SK_FALL;
            else if (c->floor<=0) { s->special=S2_SK_NORMAL; m->status&=~AIR; m->animation=5; }
            else m->y+=65536;
        }
        break;
    case S2_SK_LEDGE: {
        static const int8_t dx[]={3,8,-8,8}, dy[]={-3,-10,-12,-5};
        static const uint8_t frames[]={0xBD,0xBE,0xBF,0xD2};
        s->move=S2_MOVE_FIXED;
        if (!(s->age%7) && s->ledge<4) {
            m->x+=direction(m)*dx[s->ledge]*65536; m->y+=dy[s->ledge]*65536;
            s->frame=frames[s->ledge++];
            if (s->ledge==4) { s->special=S2_SK_NORMAL; m->status&=~(AIR|ROLL); m->radius_x=9; m->radius_y=19; m->animation=5; }
        }
        break;
    }
    default: break;
    }
}
void s2_character_before(S2CharacterState *s, S2Motion *m, uint8_t held, uint8_t pressed, const S2Contacts *c)
{
    s->held=s->native_held=held; s->pressed=s->native_pressed=pressed;
    s->before_air=!!(m->status&AIR); s->before_jumping=m->jumping;
    s->move=S2_MOVE_NATIVE; s->jump_adjustment=0;
    if (m->hurt || m->control_locked) { s->special=0; return; }
    if (s->kind==S2_CHAR_AMY) amy_before(s,m);
    if (s->kind==S2_CHAR_KNUCKLES) knuckles_before(s,m,c);
    ++s->age;
}
void s2_character_after(S2CharacterState *s, S2Motion *m, const S2Contacts *c)
{
    if (m->hurt) { s->special=0; return; }
    if (!s->before_air && (m->status&AIR) && m->jumping && s->jump_adjustment) {
        unsigned angle=(m->angle-0x40)&255;
        /* Adjustment is signed vertical on flat ground; rotate with the floor. */
        m->vx-=(int16_t)((c->sine((angle+0x40)&255)*s->jump_adjustment)>>8);
        m->vy-=(int16_t)((c->sine(angle)*s->jump_adjustment)>>8);
    }
    if (s->kind==S2_CHAR_AMY) {
        if (s->before_air && !(m->status&AIR) && s->special!=AMY_GIANT_ROLL) s->special=0;
        if (s->special==AMY_HAMMER && s->animation_changed && s->animation==0) s->special=0;
        if (s->special==AMY_AIR_HAMMER && s->animation_changed && s->animation==0x22) s->special=0;
        if (s->special==AMY_HAMMER_JUMP && s->animation_changed && s->animation==0x22) s->special=0;
        switch (s->special) {
        case AMY_HAMMER: m->animation=(s->animation==0x23)?0x23:0x28; break;
        case AMY_AIR_HAMMER: m->animation=0x24; break;
        case AMY_WHIRL: m->animation=0x25; break;
        case AMY_HAMMER_JUMP: m->animation=0x26; break;
        case AMY_GIANT: m->animation=0x27; break;
        case AMY_GIANT_ROLL:
            m->animation=(m->status&AIR)?0x29:m->inertia?0x2A:0x2B;
            if (s->animation_changed && s->animation==5) s->special=0;
            break;
        case AMY_DASH: m->animation=1; break;
        default:
            if ((m->status&AIR) && (m->animation==2 || m->animation==0x10 || m->animation==0x22))
                m->animation=m->vy < -0x100 ? 0x10:0x22;
            break;
        }
    } else if (s->kind==S2_CHAR_KNUCKLES) {
        if (s->special==S2_SK_GLIDE) {
            if (!(m->status&AIR)) { s->special=S2_SK_SLIDE; s->frame=0xCC; }
            else if (c->wall<=0 && absword(m->vx)<0x20) {
                s->special=S2_SK_CLIMB; s->wall_x=m->x; s->age=0;
                m->vx=m->vy=m->inertia=0; s->frame=0xB7;
            }
        } else if (s->special==S2_SK_FALL && !(m->status&AIR)) {
            s->special=0; m->vx=m->vy=m->inertia=0; m->animation=0x23;
        }
    }
}
int s2_character_attacking(const S2CharacterState *s, const S2Motion *m)
{
    if (s->kind==S2_CHAR_AMY) {
        unsigned a=m->animation;
        return a==2 || a==9 || (a>=0x23 && a<=0x25) || (a>=0x28 && a<=0x2A);
    }
    return m->animation==2 || m->animation==9 || (s->kind==S2_CHAR_KNUCKLES && s->special==S2_SK_GLIDE);
}
void s2_character_animate(S2CharacterState *s, S2Motion *m, const S2DonorBank *bank)
{
    if (!bank || !bank->animation_count) return;
    if (s->kind==S2_CHAR_KNUCKLES && s->special>=S2_SK_GLIDE && s->special!=S2_SK_FALL) {
        if (s->special==S2_SK_GLIDE) {
            static const uint8_t turn[]={0xC0,0xC1,0xC2,0xC3,0xC4,0xC3,0xC2,0xC1};
            s->frame=turn[((unsigned)s->turn+0x10)/32%8];
            s->render_flip=s->frame==0xC4; if (s->render_flip) s->frame=0xC0;
        } else {
            s->render_flip=!!(m->status&FACE);
            if (s->special==S2_SK_CLIMB && (s->held&(UP|DOWN)) && !(s->age&3)) {
                int frame=s->frame+((s->held&UP)?1:-1);
                s->frame=(uint8_t)(frame>0xBC?0xB7:frame<0xB7?0xBC:frame);
            }
        }
        m->frame=s->frame; return;
    }
    unsigned animation=m->animation<bank->animation_count?m->animation:5;
    s->animation_changed=0;
    if (animation!=s->animation) { s->animation=animation; s->animation_index=s->animation_timer=0; }
    const uint8_t *sequence=bank->animations[animation];
    unsigned length=bank->animation_length[animation], delay=sequence[0], add=0;
    s->render_flip=!!(m->status&FACE);
    int speed=absword(m->inertia);
    if (delay==0xFF) {
        unsigned angle=m->angle;
        if (!(angle&0x80) && angle) --angle;
        if (!(m->status&FACE)) angle=(uint8_t)~angle;
        angle=(uint8_t)(angle+0x10);
        if (angle&0x80) s->render_flip^=3;
        unsigned sector=(angle>>4)&6;
        animation=speed>=0x600?1:0;
        sequence=bank->animations[animation]; length=bank->animation_length[animation];
        add=sector*(animation?4:8);
        delay=speed<0x800?(0x800-speed)>>8:0;
    } else if (delay==0xFE) {
        animation=speed>=0x550?3:2; sequence=bank->animations[animation]; length=bank->animation_length[animation];
        delay=speed<0x400?(0x400-speed)>>8:0;
    } else if (delay==0xFD) delay=speed<0x800?(0x800-speed)>>6:0;
    if (s->animation_timer) { --s->animation_timer; m->frame=s->frame; return; }
    s->animation_timer=delay;
    if (s->animation_index+1>=length) s->animation_index=0;
    unsigned code=sequence[1+s->animation_index];
    if (code==0xFF) { s->animation_index=0; code=sequence[1]; }
    else if (code==0xFE) {
        unsigned back=sequence[2+s->animation_index];
        s->animation_index=s->animation_index>=back?s->animation_index-back:0;
        code=sequence[1+s->animation_index];
    } else if (code==0xFD) {
        s->animation=sequence[2+s->animation_index]; m->animation=(uint8_t)s->animation;
        s->animation_changed=1; s->animation_index=s->animation_timer=0; return;
    }
    if (code<0xFC && code+add<bank->count) { s->frame=(uint8_t)(code+add); ++s->animation_index; }
    m->frame=s->frame;
}
