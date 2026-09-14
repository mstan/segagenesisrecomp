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
static const GameVideo video={.configure=configure};

int main(int argc,char **argv)
{
    CHECK(argc==2);
    app_config_defaults();app_config_apply_video(&video);
    CHECK(!g_app_config.custom_widescreen && !strcmp(mode,"off"));
    const RecompLauncherCModProvider *p=app_config_video_mods(&video,argv[1]);
    CHECK(p && p->package_count(p->ctx)==1 && p->feature_count(p->ctx)==1);
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
    g_input_map.p[0].key[GB_A]=123;CHECK(app_config_save(argv[1]));
    g_input_map.p[0].key[GB_A]=999;
    CHECK(p->feature_enable(p->ctx,feature.package_id,feature.id,1));
    CHECK(p->feature_set_option(p->ctx,feature.package_id,feature.id,option.id,"21:9"));
    CHECK(p->commit(p->ctx,NULL));CHECK(g_input_map.p[0].key[GB_A]==123);
    app_config_defaults();CHECK(app_config_load(argv[1]));
    CHECK(g_app_config.custom_widescreen && g_app_config.custom_aspect==2);
    app_config_apply_video(&video);CHECK(!strcmp(mode,"21:9"));
    CHECK(p->feature_enable(p->ctx,feature.package_id,feature.id,0));CHECK(p->commit(p->ctx,NULL));
    app_config_defaults();CHECK(app_config_load(argv[1]));app_config_apply_video(&video);
    CHECK(!strcmp(mode,"off"));CHECK(!app_config_video_mods(NULL,argv[1]));
    CHECK(remove(argv[1])==0); /* this executable owns only its explicit CTest fixture */
    puts("PASS opt-in defaults, four aspect choices, provider validation, persistence and input binding preservation");
    return 0;
}
