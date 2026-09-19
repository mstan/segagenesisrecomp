/* Owner-resource feature provider. There is deliberately no ROM patch plan. */
#include "sonic2_mods.h"
#include "sonic2_party.h"
#include "sonic2_resources.h"
#if RECOMP_LAUNCHER
#include "recomp_launcher.h"
#include <stdio.h>
#include <string.h>
static const RecompLauncherCModProvider *base;
static char error[256];
static const char *ids[] = { "sonic2.amy", "sonic2.s3k" };
static const char *names[] = { "Amy Rose", "S3&K" };
static const char *features[] = { "amy", "knuckles" };
#define COPY(dst, value) snprintf(dst, sizeof(dst), "%s", value)
static int owner(const char *p, const char *f)
{
    if (p) for (int i = 0; i < 2; ++i)
        if (!strcmp(p, ids[i]) && (!f || !strcmp(f, features[i]))) return i;
    return -1;
}
static int *flag(int i) { return i ? &s2_party.s3k_enabled : &s2_party.amy_enabled; }
static int packages(void *ctx) { (void)ctx; return 2 + (base && base->package_count ? base->package_count(base->ctx) : 0); }
static int feature_count(void *ctx) { (void)ctx; return 2 + (base && base->feature_count ? base->feature_count(base->ctx) : 0); }
static const char *status(int i)
{
    if (!*flag(i)) return "Disabled";
    if (!s2_resource_verified(i)) return "Owner ROM required before character selection";
    return "Verified gameplay assets; select character in the title OPTIONS menu";
}
static int package_get(void *ctx, int i, RecompLauncherCModPackage *out)
{
    (void)ctx;
    if (i >= 2) return base && base->package_get && base->package_get(base->ctx, i - 2, out);
    if (i < 0 || !out) return 0;
    memset(out, 0, sizeof *out); COPY(out->id, ids[i]); COPY(out->name, names[i]); COPY(out->version, "0.1.0");
    COPY(out->author, i ? "SEGA / Sonic Team; host adapter contributors" : "E-122-Psi and Amy hack contributors; host adapter contributors");
    COPY(out->description, i ? "Owner-ROM-gated S3&K imports. Initial feature: Knuckles. No stages or save system are enabled."
        : "Additive Amy Rev 1.7.1 gameplay import. Leaves the Sonic 2 title and default Sonic/Tails roster unchanged.");
    COPY(out->license, "Owner-supplied assets; not redistributed"); COPY(out->status, status(i));
    out->enabled = *flag(i); return 1;
}
static int feature_get(void *ctx, int i, RecompLauncherCModFeature *out)
{
    if (i >= 2) return base && base->feature_get && base->feature_get(base->ctx, i - 2, out);
    RecompLauncherCModPackage p;
    if (!out || !package_get(ctx, i, &p)) return 0;
    memset(out, 0, sizeof *out); COPY(out->id, features[i]); COPY(out->package_id, p.id);
    COPY(out->package_name, p.name); COPY(out->package_version, p.version);
    COPY(out->name, i ? "Knuckles" : "Amy Rose"); COPY(out->author, p.author);
    COPY(out->description, p.description); COPY(out->group, "Characters"); COPY(out->status, p.status);
    out->enabled = p.enabled; return 1;
}
static int enable(void *ctx, const char *p, const char *f, int on)
{
    (void)ctx; int i = owner(p, f); error[0] = 0;
    if (i < 0) return base && base->feature_enable && base->feature_enable(base->ctx, p, f, on);
    /* Amy defaults ON even before private assets are staged; S3&K can only
     * become enabled with a verified owner image. Neither auto-picks a player. */
    if (i == S2_RESOURCE_SK && on && !s2_resource_verified(i)) {
        COPY(error, "Select and verify your Sonic 3 & Knuckles ROM first"); return 0;
    }
    *flag(i) = on != 0; s2_roster_validate(&s2_party.roster); return 1;
}
static int set_enabled(void *ctx, const char *p, int on)
{
    int i = owner(p, NULL);
    if (i >= 0) return enable(ctx, p, features[i], on);
    return base && base->set_enabled && base->set_enabled(base->ctx, p, on);
}
static int option(void *ctx, const char *p, const char *f, int n, RecompLauncherCModOption *out)
{ (void)ctx; return base && base->feature_option_get && base->feature_option_get(base->ctx, p, f, n, out); }
static int choice(void *ctx, const char *p, const char *f, const char *o, int n, RecompLauncherCModChoice *out)
{ (void)ctx; return base && base->feature_choice_get && base->feature_choice_get(base->ctx, p, f, o, n, out); }
static int set_option(void *ctx, const char *p, const char *f, const char *o, const char *v)
{ (void)ctx; return base && base->feature_set_option && base->feature_set_option(base->ctx, p, f, o, v); }
static int resource_count(void *ctx, const char *p, const char *f)
{
    (void)ctx; if (owner(p, f) >= 0) return 1;
    return base && base->feature_resource_count ? base->feature_resource_count(base->ctx, p, f) : 0;
}
static int resource_get(void *ctx, const char *p, const char *f, int n, RecompLauncherCModResource *out)
{
    (void)ctx; int i = owner(p, f);
    if (i < 0) return base && base->feature_resource_get && base->feature_resource_get(base->ctx, p, f, n, out);
    if (n || !out) return 0;
    memset(out, 0, sizeof *out); COPY(out->id, "owner-rom");
    COPY(out->label, i ? "Sonic 3 & Knuckles combined ROM" : "Amy in Sonic 2 Rev 1.7.1 ROM");
    COPY(out->description, "Verified by exact size and SHA-256. Only gameplay art is decoded; the source ROM is never patched into Sonic 2.");
    COPY(out->path, i ? s2_party.s3k_path : s2_party.amy_path);
    COPY(out->file_patterns, "*.bin,*.md"); COPY(out->file_description, "Unheadered Mega Drive ROM");
    out->required = 1; out->verified = s2_resource_verified(i);
    COPY(out->status, out->verified ? "SHA-256 verified; gameplay frames decoded" : s2_resource_error(i)); return 1;
}
static int resource_set(void *ctx, const char *p, const char *f, const char *r, const char *path)
{
    (void)ctx; int i = owner(p, f); error[0] = 0;
    if (i < 0) return base && base->feature_resource_set_path && base->feature_resource_set_path(base->ctx, p, f, r, path);
    if (!r || strcmp(r, "owner-rom")) return 0;
    int ok = s2_resource_set(i, path);
    if (!ok) COPY(error, s2_resource_error(i));
    if (!s2_resource_verified(S2_RESOURCE_SK)) s2_party.s3k_enabled = 0;
    s2_roster_validate(&s2_party.roster); return ok;
}
static int commit(void *ctx, const char *image)
{
    (void)ctx; error[0] = 0;
    if (s2_party.s3k_enabled && !s2_resource_verified(S2_RESOURCE_SK)) {
        COPY(error, "S3&K requires a verified owner ROM"); return 0;
    }
    s2_roster_validate(&s2_party.roster);
    if (base && base->commit && !base->commit(base->ctx, image)) {
        COPY(error, base->last_error ? base->last_error(base->ctx) : "Unable to save common mods"); return 0;
    }
    if (!s2_party_save()) { COPY(error, s2_party_error()); return 0; }
    return 1;
}
static int commit_netplay(void *ctx, const char *image)
{ (void)ctx; return !base || !base->commit_netplay || base->commit_netplay(base->ctx, image); }
static const char *last_error(void *ctx)
{ (void)ctx; return *error ? error : base && base->last_error ? base->last_error(base->ctx) : ""; }
const RecompLauncherCModProvider *s2_mods(const RecompLauncherCModProvider *common)
{
    static RecompLauncherCModProvider p;
    base = common; memset(&p, 0, sizeof p);
    p.package_count = packages; p.package_get = package_get; p.set_enabled = set_enabled;
    p.feature_count = feature_count; p.feature_get = feature_get; p.feature_enable = enable;
    p.feature_option_get = option; p.feature_choice_get = choice; p.feature_set_option = set_option;
    p.feature_resource_count = resource_count; p.feature_resource_get = resource_get; p.feature_resource_set_path = resource_set;
    p.commit = commit; p.commit_netplay = commit_netplay; p.last_error = last_error;
    p.archive_extension = ".genmod"; p.archive_description = "GenesisRecomp mod package";
    return &p;
}
#else
const struct RecompLauncherCModProvider *s2_mods(const struct RecompLauncherCModProvider *common) { return common; }
#endif
