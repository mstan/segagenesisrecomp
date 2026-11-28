#include "sonic_cd_widescreen.h"
#include "sonic_cd_render.h"
#include "sonic_cd_view.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint8_t g_ram[0x10000];

enum { SCENE_BUFFERS = 3 };
typedef struct {
    SegaCDScene scene;
    SegaCDScenePiece *pieces;
    size_t capacity;
    uint64_t serial;
    uint64_t capture_frame;
    int published;
} SceneBuffer;

struct SegaCDWidescreen {
    SegaCDMachine *machine;
    const SegaCDNativeProgram *program;
    const M68kStepModule **modules;
    SegaCDView view;
    SegaCDWorldMap world;
    uint8_t ram[0x10000], word[0x40000];
    uint8_t configured_code[0x10000];
    SceneBuffer scenes[SCENE_BUFFERS];
    unsigned next_scene, pending_scene;
    int active_scene;
    uint64_t serial, minimum_scene_serial, expanded, original, rejected;
    uint64_t completed_frame;
    unsigned width, configured_width, expanded_rows;
    uint32_t *pixels;
    uint8_t *sprite_line;
    int frame_ready, pending, installed, refresh_waiting;
    SegaCDWidescreenState state;
};

static void queue_capture(void *user, const M68kStepContext *cpu)
{
    SegaCDWidescreen *w = (SegaCDWidescreen *)user;
    (void)cpu;
    unsigned index;
    do { index = w->next_scene++ % SCENE_BUFFERS; }
    while ((int)index == w->active_scene);
    SceneBuffer *b = &w->scenes[index];
    SegaCDSceneSnapshot snapshot = {w->machine->devices.word, 0x40000, 0x200000,
                                    g_ram, sizeof g_ram, 0xff0000};
    b->published = 0;
    segacd_scene_init(&b->scene, b->pieces, b->capacity);
    SegaCDSceneStatus status = segacd_scene_capture(&snapshot, &b->scene);
    if (status == SEGACD_SCENE_CAPACITY) {
        size_t count = b->scene.required_piece_capacity;
        if (count > SEGACD_SCENE_MAX_PIECES ||
            count > SIZE_MAX / sizeof(*b->pieces)) { ++w->rejected; w->pending=0; return; }
        SegaCDScenePiece *pieces = (SegaCDScenePiece *)realloc(b->pieces,
                                                   count * sizeof(*pieces));
        if (!pieces) { ++w->rejected; w->pending=0; return; }
        b->pieces = pieces; b->capacity = count;
        segacd_scene_init(&b->scene, b->pieces, b->capacity);
        status = segacd_scene_capture(&snapshot, &b->scene);
    }
    if (status != SEGACD_SCENE_OK) { ++w->rejected; w->pending=0; return; }
    if(w->refresh_waiting&&!segacd_view_spawn_refresh_pending(&w->view)) {
        w->minimum_scene_serial=w->serial+1;
        w->refresh_waiting=0;
        printf("Sonic CD view refresh completed frame=%llu\n",
               (unsigned long long)w->machine->frames);
    }
    b->serial = ++w->serial;
    b->capture_frame=w->machine->frames;
    w->pending_scene=index; w->pending=1;
}

static int sat_matches(const SegaCDScene *scene, const uint8_t *sat,
                        unsigned count, int before_terminal_link)
{
    if (count != scene->native_count || count > 80) return 0;
    size_t bytes = count * 8u;
    for (size_t i=0;i<bytes;++i) {
        uint8_t value=sat[i];
        if (before_terminal_link && count==80 && i==79u*8u+3u) value=0;
        if (value!=scene->native_sat[i]) return 0;
    }
    if (!before_terminal_link && count<80 &&
        (sat[bytes]||sat[bytes+1]||sat[bytes+2]||sat[bytes+3])) return 0;
    return 1;
}

static void sat_capture(void *user, const M68kStepContext *cpu)
{
    SegaCDWidescreen *w=(SegaCDWidescreen *)user;
    if (!w->pending) return;
    SceneBuffer *b=&w->scenes[w->pending_scene];
    unsigned count=(uint8_t)cpu->state.regs.D[5];
    b->published=sat_matches(&b->scene,g_ram+0xf800,count,1);
    if (!b->published) ++w->rejected;
    w->pending=0;
}

static int detach(SegaCDWidescreen *w)
{
    if (!w->installed) return 1;
    if (!segacd_coordinator_modules(&w->machine->coordinator,SEGACD_MAIN,
                        w->program->main_modules,w->program->main_count)) return 0;
    if (!segacd_view_mark_installed(&w->view,0)) return 0;
    w->installed=0;w->configured_width=0;
    return 1;
}

static void raster(void *user, const SegaCDMachine *machine, unsigned line)
{
    SegaCDWidescreen *w=(SegaCDWidescreen *)user;
    if (w->width<=320) return;
    if (!line) {
        w->expanded_rows=0;w->frame_ready=0;w->active_scene=-1;
        memcpy(w->ram,g_ram,sizeof w->ram);
        memcpy(w->word,machine->devices.word,sizeof w->word);
        if (!w->installed || w->refresh_waiting ||
            segacd_view_spawn_refresh_pending(&w->view) ||
            !segacd_world_map_init(&w->world,w->word,
                     sizeof w->word,w->ram,sizeof w->ram)) return;
        unsigned base=(machine->vdp.reg[5]&0x7eu)<<9;
        uint8_t sat[640];
        for (unsigned i=0;i<sizeof sat;++i) sat[i]=machine->vdp.vram[(base+i)&0xffffu];
        uint64_t newest=0;
        for (unsigned i=0;i<SCENE_BUFFERS;++i) {
            SceneBuffer *b=&w->scenes[i];
            if (b->published && b->serial>newest && b->serial>=w->minimum_scene_serial &&
                b->capture_frame<=machine->frames &&
                /* The published SAT remains the display's source during a
                 * guest pause, even when no new DrawObjects call occurs.
                 * prepare() invalidates scenes on program/width changes;
                 * the complete live SAT must still match this publication. */
                sat_matches(&b->scene,sat,b->scene.native_count,0)) {
                newest=b->serial;w->active_scene=(int)i;
            }
        }
    }
    if (w->active_scene>=0 && line<224) {
        SegaCDRender render={&w->world,&w->scenes[w->active_scene].scene,
                              w->width,w->sprite_line,w->width};
        if (segacd_render_scanline(&render,&machine->vdp,line,
                             w->pixels+(size_t)line*w->width,w->width))
            ++w->expanded_rows;
    }
    if (line==223) {
        w->frame_ready=w->expanded_rows==224;
        w->completed_frame=machine->frames+1;
        if (w->frame_ready) { ++w->expanded;w->state=SEGACD_WIDESCREEN_ACTIVE; }
        else {
            ++w->original;
            if(w->installed)w->state=w->active_scene<0 ? SEGACD_WIDESCREEN_WARMING :
                                                          SEGACD_WIDESCREEN_UNAVAILABLE;
        }
    }
}

SegaCDWidescreen *segacd_widescreen_create(SegaCDMachine *machine,
                                          const SegaCDNativeProgram *program)
{
    if (!machine || !program || !program->main_modules ||
        program->main_count > SIZE_MAX/sizeof(M68kStepModule *)-1) return NULL;
    SegaCDWidescreen *w=(SegaCDWidescreen *)calloc(1,sizeof *w);
    if (!w) return NULL;
    w->machine=machine;w->program=program;w->width=320;w->active_scene=-1;
    segacd_view_init(&w->view);
    w->modules=(const M68kStepModule **)malloc((program->main_count+1)*sizeof *w->modules);
    if (!w->modules || !segacd_machine_video_observer(machine,raster,w)) {
        free(w->modules);free(w);return NULL;
    }
    memcpy(w->modules+1,program->main_modules,program->main_count*sizeof *w->modules);
    return w;
}

int segacd_widescreen_destroy(SegaCDWidescreen *w)
{
    if (!w) return 1;
    if (!detach(w)) return 0;
    if (!segacd_machine_video_observer(w->machine,NULL,NULL)) return 0;
    for (unsigned i=0;i<SCENE_BUFFERS;++i) free(w->scenes[i].pieces);
    free(w->modules);free(w->pixels);free(w->sprite_line);free(w);
    return 1;
}

int segacd_widescreen_prepare(SegaCDWidescreen *w, unsigned width)
{
    if (!w || width<320 || width>SEGACD_SONIC_CD_MAX_VIEW_WIDTH) return 0;
    width=320+((width-320)/2)*2;
    if (width!=w->width) {
        uint32_t *pixels=(uint32_t *)malloc((size_t)width*GVDP_MAX_HEIGHT*sizeof *pixels);
        uint8_t *scratch=(uint8_t *)malloc(width);
        if (!pixels || !scratch) { free(pixels);free(scratch);return 0; }
        free(w->pixels);free(w->sprite_line);
        w->pixels=pixels;w->sprite_line=scratch;w->width=width;
        w->frame_ready=0;
    }
    if (width==320) {
        if(!segacd_machine_defer_video(w->machine,0))return 0;
        if(!detach(w))return 0;
        w->state=SEGACD_WIDESCREEN_NATIVE;return 1;
    }
    /* Mechanical adoption of the common image-owner contract. Failure to
     * allocate the optional cache retains the exact eager original image. */
    (void)segacd_machine_defer_video(w->machine,1);
    if (w->installed && w->configured_width==width &&
        memcmp(w->configured_code,w->machine->devices.word,sizeof w->configured_code)==0)
        return 1;
    unsigned old_margin=w->installed ? w->view.chunk_margin : 0;
    if(w->installed&&memcmp(w->configured_code,w->machine->devices.word,
                            sizeof w->configured_code)!=0)old_margin=0;
    if (!detach(w)) return 0;
    w->refresh_waiting=0;
    w->state=SEGACD_WIDESCREEN_ORIGINAL_SCENE;
    SegaCDWorldMap map;
    if (!segacd_world_map_init(&map,w->machine->devices.word,0x40000,g_ram,sizeof g_ram) ||
        !map.background_source_valid) return 1;
    SegaCDViewSnapshot snapshot={w->machine->devices.word,0x40000,0x200000,
                                  g_ram,sizeof g_ram,0xff0000};
    SegaCDViewObservers observers={queue_capture,w,sat_capture,w};
    if (!segacd_view_configure_observed(&w->view,&snapshot,width,&observers)) {
        w->state=SEGACD_WIDESCREEN_UNAVAILABLE;return 1;
    }
    if(w->view.chunk_margin>old_margin && g_ram[0xf76c]==2) {
        if(!segacd_view_request_spawn_refresh(&w->view,&snapshot)) {
            w->state=SEGACD_WIDESCREEN_UNAVAILABLE;return 1;
        }
        w->refresh_waiting=1;
        printf("Sonic CD view refresh requested frame=%llu margin=%u->%u\n",
               (unsigned long long)w->machine->frames,old_margin,w->view.chunk_margin);
    }
    w->modules[0]=segacd_view_module(&w->view);
    if (!segacd_coordinator_modules(&w->machine->coordinator,SEGACD_MAIN,w->modules,
                                    w->program->main_count+1)) return 0;
    if (!segacd_view_mark_installed(&w->view,1)) {
        (void)segacd_coordinator_modules(&w->machine->coordinator,SEGACD_MAIN,
                             w->program->main_modules,w->program->main_count);
        return 0;
    }
    memcpy(w->configured_code,w->machine->devices.word,sizeof w->configured_code);
    w->installed=1;w->configured_width=width;
    w->state=SEGACD_WIDESCREEN_WARMING;
    for(unsigned i=0;i<SCENE_BUFFERS;++i)w->scenes[i].published=0;
    w->pending=0;w->active_scene=-1;w->frame_ready=0;
    w->minimum_scene_serial=w->serial+1;
    return 1;
}

SegaCDWidescreenState segacd_widescreen_state(const SegaCDWidescreen *w)
{return w ? w->state : SEGACD_WIDESCREEN_NATIVE;}

const uint32_t *segacd_widescreen_frame(SegaCDWidescreen *w, unsigned *width,
                                       unsigned *height, unsigned *pitch)
{
    if(!w||!width||!height||!pitch)return NULL;
    const SegaCDMachine *m=w->machine;
    *height=m->height;
    if(w->width<=320) {
        *width=m->width;*pitch=GVDP_MAX_WIDTH*sizeof(uint32_t);
        return segacd_machine_pixels(m);
    }
    *width=w->width;*pitch=w->width*sizeof(uint32_t);
    if(w->frame_ready && w->completed_frame==m->frames && *height==224)return w->pixels;
    const uint32_t *original=segacd_machine_pixels(m);
    if(!original)return NULL;
    for(unsigned y=0;y<*height && y<GVDP_MAX_HEIGHT;++y) {
        uint32_t *row=w->pixels+(size_t)y*w->width;
        for(unsigned x=0;x<w->width;++x)row[x]=0xff000000u;
        unsigned n=m->width<w->width?m->width:w->width;
        memcpy(row+(w->width-n)/2,original+(size_t)y*GVDP_MAX_WIDTH,n*sizeof(uint32_t));
    }
    return w->pixels;
}

void segacd_widescreen_stats(const SegaCDWidescreen *w, uint64_t *expanded,
                             uint64_t *original, uint64_t *rejected)
{
    if(!w)return;
    if(expanded)*expanded=w->expanded;
    if(original)*original=w->original;
    if(rejected)*rejected=w->rejected;
}
