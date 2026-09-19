#include "sonic2_save_draw.h"
#include "video/genesis_dac.h"
#include <stdio.h>
#include <string.h>

static unsigned pixel(const S2SaveAssets *a,unsigned tile,int x,int y)
{
    if (tile&0x800) x=7-x;
    if (tile&0x1000) y=7-y;
    unsigned byte=a->tiles[(tile&2047)*32+y*4+x/2];
    return x&1?byte&15:byte>>4;
}
static uint32_t color(const S2SaveAssets *a,unsigned tile,unsigned p,unsigned frame)
{
    unsigned palette=(tile>>9)&48;
    unsigned c=a->palette[palette+p];
    /* Native menu cycles Emerald colours white every third frame. */
    if (palette==32 && p && frame%3==0) c=0xEEE;
    return genesis_dac_cram_to_argb((uint16_t)c,GENESIS_DAC_NORMAL);
}
static unsigned plane_tile(const S2SaveAssets *a,const S2SaveView *v,int tx,int ty)
{
    if (ty>=1 && ty<13 && tx>=1 && tx<12) return a->layout[273+(ty-1)*11+tx-1];
    if (ty>=1 && ty<13 && tx>=116 && tx<127) return a->layout[273+(ty-1)*11+tx-116];
    if (tx<12 || tx>=116 || ty<1 || ty>=25) return 0;
    int card=(tx-12)/13, x=(tx-12)%13;
    if (ty>=2 && ty<9 && x>=1 && x<11) {
        unsigned index=(ty-2)*10+x-1;
        return v->data.slots[card].state?a->static_card[(v->frame/4)%4][index]:a->new_card[index];
    }
    if (ty>=14) return a->layout[130+(ty-14)*13+x];
    return a->layout[(ty-1)*13+x];
}
static void sprite(const S2SaveAssets *a,const S2SaveView *v,unsigned frame,
                   int x,int y,int line,uint32_t *out,int width)
{
    if (frame>=S2_MENU_MAP_FRAMES) return;
    const S2MenuFrame *f=&a->frames[frame];
    /* The native SAT gives earlier pieces priority. */
    for (int i=(int)f->count-1;i>=0;--i) {
        const S2MenuPiece *p=&f->pieces[i]; int row=line-y-p->y;
        if (row<0 || row>=p->height*8) continue;
        for (int px=0;px<p->width*8;++px) {
            int dst=x+p->x+px;
            if (dst<0 || dst>=width) continue;
            int sx=(p->tile&0x800)?p->width*8-1-px:px;
            int sy=(p->tile&0x1000)?p->height*8-1-row:row;
            unsigned tile=(p->tile&~0x1800u)+(sx/8)*p->height+sy/8;
            unsigned ink=pixel(a,tile,sx%8,sy%8);
            if (ink) out[dst]=color(a,tile,ink,v->frame);
        }
    }
}
static unsigned glyph(char c)
{
    if (c>='A' && c<='Z') return 30+c-'A';
    if (c>='0' && c<='9') return 16+c-'0';
    return 0;
}
static void text(const S2SaveAssets *a,int x,int y,const char *s,int line,
                 uint32_t *out,int width)
{
    if (line<y || line>=y+8) return;
    for (;*s;++s,x+=8) {
        unsigned c=glyph(*s); if (!c) continue;
        for (int px=0;px<8;++px) if (x+px>=0 && x+px<width) {
            /* sub_D9F4: ArtTile_Save_Text-$10 + LEVELSELECT character. */
            unsigned tile=0x552+c,ink=pixel(a,tile,px,line-y);
            if (ink) out[x+px]=color(a,tile|0x2000,ink,1);
        }
    }
}
static void centered(const S2SaveAssets *a,int x,int y,const char *s,int line,uint32_t *out,int width)
{ text(a,x-(int)strlen(s)*4,y,s,line,out,width); }
void s2_save_draw_notice(const S2SaveAssets *a,const char *message,int line,uint32_t *out,int width)
{
    if (!a || line<212 || line>=224) return;
    for (int x=0;x<width;++x) out[x]=0xFF000000;
    centered(a,width/2,214,message,line,out,width);
}
void s2_save_draw_line(const S2SaveAssets *a,const S2SaveView *v,int line,uint32_t *out,int width)
{
    if (!a || !v || line<0 || line>=224 || width<=0) return;
    int left=(width-320)/2;
    for (int x=0;x<width;++x) {
        int bx=(x-left+v->scroll)%320; if (bx<0) bx+=320;
        unsigned tile=a->background[(line/8)*40+bx/8];
        unsigned ink=pixel(a,tile,bx%8,line%8);
        out[x]=color(a,tile,ink,1);
        int ax=x-left+v->scroll;
        if (ax>=0 && ax<1024) {
            tile=plane_tile(a,v,ax/8,line/8); ink=pixel(a,tile,ax%8,line%8);
            if (ink) out[x]=color(a,tile,ink,1);
        }
    }
    int base=left-v->scroll;
    sprite(a,v,4,base+48,72,line,out,width); /* fixed Sonic AND Tails, even No Save */
    /* sub_D9F4 destinations C06/CEC use a 128-tile (256-byte) stride:
     * row 12 = y96, immediately below the small No Save/Delete cards. */
    centered(a,base+48,96,"NO SAVE",line,out,width);
    for (unsigned i=0;i<S2_SAVE_SLOTS;++i) {
        int x=base+144+104*(int)i;
        const S2CampaignSlot *slot=&v->data.slots[i];
        sprite(a,v,4,x,136,line,out,width);
        for (unsigned e=0;e<7;++e) if (slot->emeralds&(1u<<e)) sprite(a,v,16+e,x,136,line,out,width);
        char label[20]; snprintf(label,sizeof label,"FILE %u",i+1);
        centered(a,x+8,174,label,line,out,width);
        if (slot->state && slot->stage<S2_CAMPAIGN_STAGES) {
            /* Replace the S3 level thumbnail with the requested S2 zone/act. */
            if (line>=16 && line<72) for (int px=x-40;px<x+40;++px)
                if (px>=0 && px<width) out[px]=color(a,0,0,1);
            const S2CampaignStage *s=&s2_campaign_stages[slot->stage];
            const char *space=strchr(s->name,' ');
            if (space) {
                size_t first=(size_t)(space-s->name);
                snprintf(label,sizeof label,"%.*s",(int)first,s->name);
                centered(a,x,22,label,line,out,width);
                centered(a,x,34,space+1,line,out,width);
            } else centered(a,x,28,s->name,line,out,width);
            snprintf(label,sizeof label,"ACT %u",s->act);
            centered(a,x,52,label,line,out,width);
            if (slot->state==S2_SAVE_COMPLETE) centered(a,x+8,158,"CLEAR",line,out,width);
        }
        if (v->selection==i+1 && slot->state==S2_SAVE_COMPLETE && !v->erase && (v->frame&16))
            sprite(a,v,15,x,136,line,out,width);
    }
    sprite(a,v,13,base+968,88,line,out,width);
    centered(a,base+968,96,"DELETE",line,out,width);
    /* Same native selector positions, 104px spacing, and 8px scroll steps. */
    int selector=v->selection?144+(int)(v->selection-1)*104:48;
    if (v->selection==9) selector=968;
    if (!(v->frame&4)) sprite(a,v,v->selection==0 || v->selection==9?2:1,
        base+selector,98,line,out,width);
    sprite(a,v,3,left+160,204,line,out,width);
    const char *hint=v->notice;
    if (!hint || !*hint) {
        if (v->confirm) hint="A CONFIRM DELETE   B CANCEL";
        else if (v->erase) hint="CHOOSE FILE TO DELETE   B CANCEL";
        else if (v->read_only) hint="SAVE FILE PROTECTED   NO SAVE OK";
        else if (v->selection>=1 && v->selection<=8 && v->data.slots[v->selection-1].state)
            hint=s2_campaign_stages[v->data.slots[v->selection-1].stage].name;
        else hint="LEFT RIGHT SELECT   A START   B BACK";
    }
    centered(a,left+160,216,hint,line,out,width);
}
