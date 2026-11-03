#include "app_config.h"
#include "game_video.h"
#include "input_map.h"
#include "recomp_launcher.h"
#include "common/launcher_model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do {if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);return 1;}}while(0)
InputMap g_input_map;
/* Unrelated NES-only binding bridge; no platform backend needed for this test. */
void launcher_binds_set_zapper(int mouse_enabled,int crosshair) {(void)mouse_enabled;(void)crosshair;}
const char *input_button_name(GenesisButton b) {
    static const char *names[]={"Up","Down","Left","Right","A","B","C","Start","X","Y","Z","Mode"};
    return names[b];
}
static char mode[32];
static int configure(const char *value) {snprintf(mode,sizeof mode,"%s",value);return 1;}
static int enabled(void) {return strcmp(mode,"off")!=0;}
static int width(int dw,int dh,int nw,int nh) {(void)dw;(void)dh;(void)nh;return nw;}
static const GameVideo video={.configure=configure,.enabled=enabled,.width=width};
static int presentation_enabled;
static void presentation_configure(int on){presentation_enabled=on;}
static void presentation_capture(const struct GVDP *v,int l,const uint32_t *p,int w,int n,const struct GVDPRasterRow *r)
{(void)v;(void)l;(void)p;(void)w;(void)n;(void)r;}
static int interpolate(double t,uint32_t *p,int s,int h){(void)t;(void)p;(void)s;(void)h;return 0;}
static const GameVideo interpolated_video={.configure=configure,.enabled=enabled,.width=width,.presentation_configure=presentation_configure,
    .presentation_capture=presentation_capture,.interpolate=interpolate};

int main(int argc,char **argv)
{
    CHECK(argc==2);
    app_config_defaults();app_config_apply_video(&video);
    CHECK(!g_app_config.custom_widescreen && !strcmp(mode,"off"));
    const RecompLauncherCModProvider *p=app_config_video_mods(&video,argv[1]);
    CHECK(p && p->package_count(p->ctx)==3 && p->feature_count(p->ctx)==3);
    CHECK(g_app_config.replay_diagnostics);
    RecompLauncherCModFeature diagnostics;
    CHECK(p->feature_get(p->ctx,1,&diagnostics));
    CHECK(diagnostics.enabled && !strcmp(diagnostics.id,"replay") && diagnostics.option_count==0);
    RecompLauncherCGameInfo game={0};game.name="Sonic the Hedgehog";game.mods=p;
    LauncherModel model;launcher_model_init(&model,NULL,&game,NULL);
#if RECOMP_UI_ENABLE_MODS || EXPECT_CUSTOM_VIDEO_MODS
    CHECK(model.mods==p); /* Catches Sonic forgetting RECOMP_UI_ENABLE_MODS. */
#else
    CHECK(model.mods==NULL); /* Other consumers may deliberately omit Mods. */
#endif
    RecompLauncherCModFeature feature;RecompLauncherCModOption option;RecompLauncherCModChoice choice;
    CHECK(p->feature_get(p->ctx,0,&feature));CHECK(!feature.enabled && feature.option_count==1);
    CHECK(p->feature_option_get(p->ctx,feature.package_id,feature.id,0,&option));
    CHECK(option.choice_count==4 && !strcmp(option.value,"fit"));
    for(int i=0;i<4;++i) {
        CHECK(p->feature_choice_get(p->ctx,feature.package_id,feature.id,option.id,i,&choice));
        CHECK(p->feature_set_option(p->ctx,feature.package_id,feature.id,option.id,choice.value));
        app_config_apply_video(&video);CHECK(!strcmp(mode,"off")); /* choosing an aspect does not opt in */
        CHECK(p->feature_enable(p->ctx,feature.package_id,feature.id,1));
        app_config_apply_video(&video);CHECK(!strcmp(mode,app_config_aspect_mode(i)));
        CHECK(p->feature_enable(p->ctx,feature.package_id,feature.id,0));
    }
    CHECK(!p->feature_enable(p->ctx,"bad",feature.id,1));
    CHECK(!p->feature_set_option(p->ctx,feature.package_id,feature.id,option.id,"garbage"));
    CHECK(!p->feature_choice_get(p->ctx,feature.package_id,feature.id,option.id,4,&choice));
    /* Simulate a controller rebind saved by the launcher's input bridge. */
    g_input_map.p[0].key[GB_A]=123;g_app_config.audio_freq=44100;
    CHECK(app_config_save(argv[1]));
    g_input_map.p[0].key[GB_A]=999;
    CHECK(p->feature_enable(p->ctx,feature.package_id,feature.id,1));
    CHECK(p->feature_set_option(p->ctx,feature.package_id,feature.id,option.id,"21:9"));
    CHECK(p->commit(p->ctx,NULL));CHECK(g_input_map.p[0].key[GB_A]==123);
    app_config_defaults();CHECK(app_config_load(argv[1]));
    CHECK(g_app_config.custom_widescreen && g_app_config.custom_aspect==2);
    CHECK(g_app_config.audio_freq==44100); /* Mods commits preserve Audio. */
    app_config_apply_video(&video);CHECK(!strcmp(mode,"21:9"));
    CHECK(p->feature_enable(p->ctx,feature.package_id,feature.id,0));CHECK(p->commit(p->ctx,NULL));
    app_config_defaults();CHECK(app_config_load(argv[1]));app_config_apply_video(&video);
    CHECK(!strcmp(mode,"off"));
    CHECK(p->feature_enable(p->ctx,"playtest-diagnostics","replay",0));
    CHECK(p->commit(p->ctx,NULL));app_config_defaults();CHECK(app_config_load(argv[1]));
    CHECK(!g_app_config.replay_diagnostics);
    p=app_config_video_mods(NULL,argv[1]);CHECK(p && p->feature_count(p->ctx)==2);
    CHECK(p->feature_get(p->ctx,0,&diagnostics) && !strcmp(diagnostics.id,"replay"));
    /* Old installations without a Diagnostics setting inherit default-on. */
    FILE *legacy=fopen(argv[1],"wb");CHECK(legacy);fputs("[video]\nwindow_scale=2\n[mods.widescreen]\nenabled=0\n",legacy);fclose(legacy);
    app_config_defaults();CHECK(app_config_load(argv[1]) && g_app_config.audio_freq==48000);
    const int rates[]={32040,32000,44100,48000};
    for(unsigned r=0;r<sizeof rates/sizeof rates[0];++r) {
        g_app_config.audio_freq=rates[r];CHECK(app_config_save(argv[1]));
        app_config_defaults();CHECK(app_config_load(argv[1]) && g_app_config.audio_freq==rates[r]);
    }
    legacy=fopen(argv[1],"wb");CHECK(legacy);
    fputs("[audio]\nfrequency=-1\n[mods.widescreen]\nenabled=0\n",legacy);fclose(legacy);
    CHECK(app_config_load(argv[1]) && g_app_config.audio_freq==48000);
    app_config_defaults();CHECK(app_config_load(argv[1]) && g_app_config.replay_diagnostics);
    p=app_config_video_mods(&video,argv[1]);
    /* GameSpec.video_default: a game may enable its renderer on a fresh
     * install, while a saved choice (including off) still wins. */
    CHECK(!app_config_set_video_default("garbage"));
    app_config_defaults();CHECK(!g_app_config.custom_widescreen); /* bad input leaves opt-in */
    CHECK(app_config_set_video_default("fit"));
    app_config_defaults();CHECK(g_app_config.custom_widescreen && g_app_config.custom_aspect==0);
    app_config_apply_video(&video);CHECK(!strcmp(mode,"fit"));
    CHECK(p->feature_get(p->ctx,0,&feature) && feature.enabled);
    CHECK(app_config_load(argv[1]));CHECK(!g_app_config.custom_widescreen); /* saved off */
    app_config_apply_video(&video);CHECK(!strcmp(mode,"off"));
    CHECK(remove(argv[1])==0);
    app_config_defaults();CHECK(!app_config_load(argv[1])); /* no settings file */
    app_config_apply_video(&video);CHECK(!strcmp(mode,"fit"));
    CHECK(p->feature_get(p->ctx,0,&feature) && feature.enabled);
    CHECK(p->feature_option_get(p->ctx,feature.package_id,feature.id,0,&option));
    CHECK(!strcmp(option.value,"fit"));
    FILE *f=fopen(argv[1],"wb");CHECK(f);
    fputs("[mods.widescreen]\nenabled=0\naspect=32:9\n",f);CHECK(fclose(f)==0);
    app_config_defaults();CHECK(app_config_load(argv[1]));
    CHECK(!g_app_config.custom_widescreen && g_app_config.custom_aspect==3);
    CHECK(p->feature_get(p->ctx,0,&feature) && !feature.enabled);
    CHECK(p->feature_option_get(p->ctx,feature.package_id,feature.id,0,&option));
    CHECK(!strcmp(option.value,"32:9"));
    CHECK(p->commit(p->ctx,NULL)); /* provider must not replace an explicit off */
    app_config_defaults();CHECK(app_config_load(argv[1]));
    app_config_apply_video(&video);CHECK(!strcmp(mode,"off") && g_app_config.custom_aspect==3);
    f=fopen(argv[1],"wb");CHECK(f);
    fputs("[mods.widescreen]\nenabled=1\naspect=16:9\n",f);CHECK(fclose(f)==0);
    app_config_defaults();CHECK(app_config_load(argv[1]));
    app_config_apply_video(&video);CHECK(!strcmp(mode,"16:9"));
    CHECK(p->commit(p->ctx,NULL));
    app_config_defaults();CHECK(app_config_load(argv[1]));
    CHECK(g_app_config.custom_widescreen && g_app_config.custom_aspect==1);
    f=fopen(argv[1],"wb");CHECK(f);
    fputs("[mods.widescreen]\naspect=21:9\n",f);CHECK(fclose(f)==0);
    app_config_defaults();CHECK(app_config_load(argv[1]));
    CHECK(g_app_config.custom_widescreen && g_app_config.custom_aspect==2); /* absent enabled inherits on */
    f=fopen(argv[1],"wb");CHECK(f);
    fputs("[audio]\nvolume = 50\n",f);CHECK(fclose(f)==0); /* settings without the mod key */
    app_config_defaults();CHECK(app_config_load(argv[1]));
    CHECK(g_app_config.custom_widescreen && g_app_config.custom_aspect==0 && g_app_config.volume==50);
    CHECK(app_config_set_video_default("21:9"));
    app_config_defaults();CHECK(g_app_config.custom_widescreen && g_app_config.custom_aspect==2);
    CHECK(!app_config_set_video_default("garbage"));
    app_config_defaults();CHECK(g_app_config.custom_widescreen && g_app_config.custom_aspect==2);
    CHECK(app_config_set_video_default(NULL));
    app_config_defaults();CHECK(!g_app_config.custom_widescreen && g_app_config.custom_aspect==0);

    p=app_config_video_mods(&interpolated_video,argv[1]);
    CHECK(p->feature_count(p->ctx)==4);
    CHECK(!g_app_config.presentation_rate && !g_app_config.audio_filter_bypass);
    CHECK(p->feature_get(p->ctx,2,&feature) && !strcmp(feature.id,"interpolation") && !feature.enabled);
    for(int n=0;n<4;++n) {
        CHECK(p->feature_choice_get(p->ctx,"high-refresh","interpolation","rate",n,&choice));
        CHECK(p->feature_set_option(p->ctx,"high-refresh","interpolation","rate",choice.value));
        CHECK(g_app_config.presentation_rate==app_config_presentation_rate(n));
        app_config_apply_video(&interpolated_video);CHECK(presentation_enabled==(n!=0));
        CHECK(!strcmp(mode,"off")); /* Native interpolation cannot enable wide simulation. */
    }
    CHECK(!p->feature_set_option(p->ctx,"high-refresh","interpolation","rate","2000"));
    CHECK(p->feature_enable(p->ctx,"audio-output","unfiltered",1));
    CHECK(p->commit(p->ctx,NULL));app_config_defaults();CHECK(app_config_load(argv[1]));
    CHECK(g_app_config.presentation_rate==-1 && g_app_config.audio_filter_bypass);
    CHECK(p->feature_enable(p->ctx,"high-refresh","interpolation",0));
    CHECK(p->feature_enable(p->ctx,"audio-output","unfiltered",0));
    CHECK(p->commit(p->ctx,NULL));app_config_defaults();CHECK(app_config_load(argv[1]));
    CHECK(!g_app_config.presentation_rate && !g_app_config.audio_filter_bypass);
    /* A native row owner is not an elective viewport/widescreen feature. */
    const GameVideo renderer_only={.renderer_enabled=enabled};
    CHECK(!game_video_has_viewport(&renderer_only));
    p=app_config_video_mods(&renderer_only,argv[1]);
    CHECK(p && p->package_count(p->ctx)==2 && p->feature_count(p->ctx)==2);
    CHECK(p->feature_get(p->ctx,0,&diagnostics) && !strcmp(diagnostics.id,"replay"));
    CHECK(p->feature_get(p->ctx,1,&feature) && !strcmp(feature.id,"unfiltered"));
    CHECK(!p->feature_get(p->ctx,2,&feature)); /* no viewport/widescreen feature */
    app_config_apply_video(&renderer_only); /* no configure callback */
    CHECK(remove(argv[1])==0); /* this executable owns only its explicit CTest fixture */
    puts("PASS opt-in and game default, saved-off precedence, four aspect choices, provider validation, persistence and input binding preservation");
    return 0;
}
