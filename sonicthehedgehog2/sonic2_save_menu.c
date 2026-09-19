#include "sonic2_save_menu.h"
#include "sonic2_save_draw.h"
#include "sonic2_party.h"
#include "sonic2_resources.h"
#include "genesis_runtime.h"
#include <stdio.h>
#include <string.h>
#if GENESIS_HAS_RECOMP_NET
#include "netplay/genesis_netplay.h"
#endif

static S2CampaignStore store;
static S2SaveView view;
static int menu,menu_ready,exit_action,session=-1,dirty;
static unsigned replay_zone;
static uint16_t word(unsigned a) { return (uint16_t)((g_ram[a]<<8)|g_ram[a+1]); }
static void putword(unsigned a,unsigned v) { g_ram[a]=(uint8_t)(v>>8); g_ram[a+1]=(uint8_t)v; }
int s2_save_menu_enabled(void)
{
#if GENESIS_HAS_RECOMP_NET
    if (genesis_netplay_active()) return 0;
#endif
    return s2_party.save_menu_enabled && s2_resource_save_assets()!=NULL;
}
void s2_save_menu_load(const char *settings)
{
    menu=menu_ready=exit_action=dirty=0; session=-1;
    memset(&view,0,sizeof view); memset(&store,0,sizeof store);
    char path[1024]; int length=snprintf(path,sizeof path,"%s",settings?settings:"settings.ini");
    if (length<0 || length>=(int)sizeof path) { store.read_only=1; return; }
    char *slash=strrchr(path,'/'),*back=strrchr(path,'\\');
    char *base=back && (!slash || back>slash)?back+1:slash?slash+1:path;
    length=snprintf(base,sizeof path-(size_t)(base-path),"sonic2-campaign.sav");
    if (length<0 || length>=(int)(sizeof path-(size_t)(base-path))) { store.read_only=1; return; }
    s2_campaign_open(&store,path); view.data=store.data;
    view.read_only=store.read_only;
}
static unsigned emeralds(void)
{
    unsigned mask=0;
    for (unsigned i=0;i<7;++i) if (g_ram[0xFFB2+i]) mask|=1u<<i;
    return mask;
}
static int flush(void)
{
    if (!dirty) return 1;
    if (!s2_campaign_commit(&store,&view.data)) {
        view.notice="SAVE FAILED   SELECT FILE TO RETRY";
        view.read_only=store.read_only; return 0;
    }
    dirty=0; view.notice=NULL; return 1;
}
static int campaign_mode(unsigned mode)
{
    return session>=0 && !word(0xFFD8) && !word(0xFFF0) && !g_ram[0xFE08] &&
        (g_ram[0xF600]&0x7F)==mode;
}
static void save_transition(uint16_t destination)
{
    if (!campaign_mode(12)) return;
    S2CampaignSlot *slot=&view.data.slots[session];
    if (!s2_campaign_advance(slot,destination)) return;
    s2_campaign_collect(slot,emeralds()); dirty=1; flush();
}
static void begin_menu(void)
{
    menu=1; menu_ready=exit_action=0; session=-1;
    view.selection=1; view.scroll=0; view.frame=0; view.erase=view.confirm=0;
    replay_zone=view.data.slots[0].state?s2_campaign_stages[view.data.slots[0].stage].zone:0;
    if (!store.ready) view.notice="SAVE FILE INVALID   USE NO SAVE";
    else if (store.read_only) view.notice="SAVE FILE PROTECTED   NO SAVE OK";
    g_ram[0xF600]=0x24; /* Native MenuScreen initializes input/music/frame pacing. */
}
static void launch(void)
{
    unsigned stage=0,mask=0;
    if (session>=0) { stage=view.data.slots[session].stage; mask=view.data.slots[session].emeralds; }
    putword(0xFE10,s2_campaign_stages[stage].native_id);
    putword(0xFFD8,0); putword(0xFF8A,0); /* Two_player_mode and its copy */
    putword(0xFE02,0); putword(0xFFF0,0);
    g_ram[0xFE30]=g_ram[0xFEE0]=0; /* no starpost resume */
    g_ram[0xFE08]=0;
    g_ram[0xFFB0]=0; g_ram[0xFFB1]=(uint8_t)s2_campaign_emerald_count(mask);
    memset(g_ram+0xFFB2,0,8);
    unsigned next=0;
    for (unsigned i=0;i<7;++i) if (mask&(1u<<i)) g_ram[0xFFB2+i]=0xFF;
    while (next<7 && (mask&(1u<<next))) ++next;
    putword(0xFE16,(next%7)<<8);
    /* TitleScreen already supplied native 3 lives / zero score, rings, timer,
     * continues and extra-life thresholds before it entered this menu.
     * The following native Level route owns object and party initialization. */
    g_ram[0xF600]=12; menu=menu_ready=exit_action=0;
}
static void controls(void)
{
    ++view.frame; menu_ready=1;
    unsigned press=g_ram[0xF605];
    /* Consume both native Start bits; only a deliberate host exit sets one. */
    g_ram[0xF605]&=0x7F; g_ram[0xF607]&=0x7F;
    int target=(int)view.selection*104-120;
    if (target<0) target=0; if (target>704) target=704;
    if (view.scroll<target) { view.scroll+=8; if (view.scroll>target) view.scroll=target; }
    if (view.scroll>target) { view.scroll-=8; if (view.scroll<target) view.scroll=target; }
    if (press&16) {
        if (view.confirm) view.confirm=0;
        else if (view.erase) view.erase=0;
        else { exit_action=1; g_ram[0xF605]|=128; }
        return;
    }
    if (view.scroll!=target) return;
    if (!view.confirm && (press&12)) {
        unsigned old=view.selection;
        if ((press&12)==4 && view.selection>(view.erase?1u:0u)) --view.selection;
        if ((press&12)==8 && view.selection<9) ++view.selection;
        if (old!=view.selection && view.selection>=1 && view.selection<=8) {
            const S2CampaignSlot *s=&view.data.slots[view.selection-1];
            replay_zone=s->state?s2_campaign_stages[s->stage].zone:0;
        }
        return;
    }
    if (!view.erase && view.selection>=1 && view.selection<=8) {
        S2CampaignSlot *s=&view.data.slots[view.selection-1];
        if (s->state==S2_SAVE_COMPLETE) {
            if ((press&3)==1) replay_zone=(replay_zone+1)%S2_CAMPAIGN_ZONES;
            if ((press&3)==2) replay_zone=(replay_zone+S2_CAMPAIGN_ZONES-1)%S2_CAMPAIGN_ZONES;
        }
    }
    if (!(press&0xE0)) return;
    if (view.selection==9) { view.erase=!view.erase; view.confirm=0; return; }
    if (view.erase) {
        if (!view.selection || store.read_only || !view.data.slots[view.selection-1].state) return;
        if (!view.confirm) { view.confirm=1; return; }
        S2CampaignData candidate=view.data;
        s2_campaign_delete(&candidate,view.selection-1);
        if (!s2_campaign_commit(&store,&candidate)) { view.notice="DELETE FAILED   FILE PRESERVED"; return; }
        view.data=candidate; dirty=0; view.erase=view.confirm=0; view.notice=NULL;
        return;
    }
    if (!view.selection) { session=-1; exit_action=2; g_ram[0xF605]|=128; return; }
    if (!store.ready) return;
    unsigned selected=view.selection-1;
    S2CampaignData before=view.data;
    if (!view.data.slots[selected].state) {
        if (store.read_only) return;
        s2_campaign_new(&view.data,selected); dirty=1;
    } else if (view.data.slots[selected].state==S2_SAVE_COMPLETE) {
        s2_campaign_select_zone(&view.data.slots[selected],replay_zone);
        if (!store.read_only && memcmp(&before,&view.data,sizeof before)) dirty=1;
    }
    if (!store.read_only && !flush()) return;
    session=(int)selected; exit_action=2; g_ram[0xF605]|=128;
}
int s2_save_menu_hook(uint32_t pc)
{
    if (!s2_save_menu_enabled()) return 0;
    switch (pc) {
    case 0x3998: /* TitleScreen: end previous campaign session, including game over. */
        flush(); session=-1; menu=menu_ready=0; return 0;
    case 0x3CF4: /* One-player title branch, after native fresh-game reset. */
        begin_menu(); return 0;
    case 0x142AE: /* Results: next zone/act + inactive flag already committed. */
        save_transition(word(0xFE10)); return 0;
    case 0x3A898: /* Sky Chase: WFZ destination already written. */
        save_transition(word(0xFE10)); return 0;
    case 0x3AC54: /* Wing Fortress: DEZ destination already written. */
        if (word(0xFE10)==0x0E00) save_transition(0x0E00);
        return 0;
    case 0x36198: /* Emerald flags/count already committed; no destination write. */
        if (campaign_mode(16)) {
            s2_campaign_collect(&view.data.slots[session],emeralds()); dirty=1; flush();
        }
        return 0;
    case 0x9C7C: /* Native EndingSequence, before any clear/reset. */
        if (campaign_mode(0x20) && s2_campaign_complete(&view.data.slots[session])) {
            s2_campaign_collect(&view.data.slots[session],emeralds()); dirty=1; flush();
        }
        return 0;
    default: break;
    }
    if (!menu || g_ram[0xF600]!=0x24) return 0;
    if (pc==0x90E0) { controls(); return 1; }
    if (pc==0x9186 || pc==0x91F8) return 1;
    if (pc==0x909A) {
        if (exit_action==2) launch();
        else if (exit_action==1) { menu=menu_ready=0; g_ram[0xF600]=4; }
        else recomp_tail_call(0x9060);
        return 1;
    }
    return 0;
}
int s2_save_menu_overlay(int line,uint32_t *out,int width)
{
    if (!s2_save_menu_enabled()) return 0;
    if (!menu || !menu_ready || g_ram[0xF600]!=0x24) {
        if (dirty && session>=0) s2_save_draw_notice(s2_resource_save_assets(),"CAMPAIGN SAVE FAILED",line,out,width);
        return 0;
    }
    S2SaveView display=view;
    if (!view.erase && view.selection>=1 && view.selection<=8 &&
        display.data.slots[view.selection-1].state==S2_SAVE_COMPLETE)
        s2_campaign_select_zone(&display.data.slots[view.selection-1],replay_zone);
    s2_save_draw_line(s2_resource_save_assets(),&display,line,out,width);
    return 1;
}
