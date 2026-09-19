#include "sonic2_resources.h"
#include "sonic2_party.h"
#include "sonic2_mods.h"
#include "recomp_launcher.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int delegated;
static int count(void *ctx) { assert(ctx == &delegated); return 1; }
static int enable(void *ctx, const char *p, const char *f, int on)
{ assert(ctx == &delegated && !strcmp(p,"base") && !strcmp(f,"wide") && on); ++delegated; return 1; }
int main(int argc, char **argv)
{
    assert(argc == 2 || argc == 4);
    s2_party_load(argv[1]); s2_resources_load(argv[1]);
    assert(s2_party.amy_enabled && !s2_party.s3k_enabled && !s2_party.save_menu_enabled);
    RecompLauncherCModProvider base = {0}; base.ctx=&delegated; base.package_count=count; base.feature_count=count; base.feature_enable=enable;
    const RecompLauncherCModProvider *p=s2_mods(&base);
    assert(p->package_count(p->ctx)==3 && p->feature_count(p->ctx)==4);
    assert(p->feature_enable(p->ctx,"base","wide",1) && delegated==1);
    assert(!p->feature_enable(p->ctx,"sonic2.s3k","knuckles",1));
    assert(!s2_party.s3k_enabled && *p->last_error(p->ctx));
    assert(!p->feature_enable(p->ctx,"sonic2.s3k","save-menu",1));
    assert(!p->feature_enable(p->ctx,"sonic2.s3k","unknown-feature",1));
    assert(delegated==1 && !s2_party.save_menu_enabled);
    assert(!s2_resource_set(2,"missing") && !s2_resource_set(0,"missing"));
    RecompLauncherCModFeature f;
    assert(p->feature_get(p->ctx,0,&f) && f.enabled && !strcmp(f.id,"amy"));
    assert(p->feature_get(p->ctx,1,&f) && !f.enabled && !strcmp(f.package_name,"S3&K"));
    assert(p->feature_get(p->ctx,2,&f) && !f.enabled && !strcmp(f.id,"save-menu"));
    assert(p->feature_resource_count(p->ctx,"sonic2.s3k","knuckles")==1);
    if (argc==4) {
        assert(s2_resource_set(S2_RESOURCE_AMY,argv[2]));
        assert(s2_resource_set(S2_RESOURCE_SK,argv[3]));
        assert(s2_resource_bank(S2_RESOURCE_AMY)->count==253);
        assert(s2_resource_bank(S2_RESOURCE_AMY)->animation_count==44);
        assert(s2_resource_bank(S2_RESOURCE_SK)->count==251);
        assert(s2_resource_bank(S2_RESOURCE_SK)->animation_count==37);
        assert(s2_resource_owner_verified(S2_RESOURCE_SK) && s2_resource_save_assets());
        const S2SaveAssets *menu=s2_resource_save_assets();
        assert(!s2_resource_set(S2_RESOURCE_SK,argv[2]));
        assert(s2_resource_save_assets()==menu && s2_resource_owner_verified(S2_RESOURCE_SK));
        const S2DonorBank *before=s2_resource_bank(S2_RESOURCE_AMY);
        uint8_t *pixels=before->frames[1].pixels;
        assert(!s2_resource_set(S2_RESOURCE_AMY,argv[3]));
        assert(s2_resource_verified(S2_RESOURCE_AMY) && before->frames[1].pixels==pixels);
        assert(!strcmp(s2_party.amy_path,argv[2]));
        assert(p->feature_enable(p->ctx,"sonic2.s3k","knuckles",1));
        assert(s2_party.s3k_enabled);
        assert(!strcmp(s2_party.roster.character[0],"sonic"));
        assert(!strcmp(s2_party.roster.character[1],"tails"));
        assert(p->commit_netplay(p->ctx,"unused") && s2_party.s3k_enabled);
        assert(p->feature_enable(p->ctx,"sonic2.s3k","knuckles",0));
        assert(p->feature_enable(p->ctx,"sonic2.s3k","save-menu",1));
        assert(s2_party.save_menu_enabled && !s2_party.s3k_enabled);
        assert(!p->commit_netplay(p->ctx,"unused") && *p->last_error(p->ctx));
        assert(p->feature_get(p->ctx,1,&f) && !f.enabled);
        assert(p->feature_get(p->ctx,2,&f) && f.enabled);
        assert(p->feature_resource_count(p->ctx,"sonic2.s3k","save-menu")==1);
        assert(p->feature_enable(p->ctx,"sonic2.s3k","save-menu",0));
        assert(p->set_enabled(p->ctx,"sonic2.s3k",1));
        assert(s2_party.s3k_enabled && s2_party.save_menu_enabled);
        assert(p->set_enabled(p->ctx,"sonic2.s3k",0));
        assert(!s2_party.s3k_enabled && !s2_party.save_menu_enabled);
    }
    s2_resources_shutdown();
    assert(!s2_resource_verified(0) && !s2_resource_verified(1));
    assert(!s2_resource_owner_verified(1) && !s2_resource_save_assets());
    puts("Owner-resource identity, atomic replacement and composed Mods provider passed");
    return 0;
}
