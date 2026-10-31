#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS /* Standard getenv, process-local read only. */
#endif
#include "genesis_gpu_raster.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef GENESIS_GPU_RASTER
#define GENESIS_GPU_RASTER 1
#endif

#if GENESIS_GPU_RASTER && defined(SDL_VIDEO_OPENGL) && !defined(__EMSCRIPTEN__)
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:5105) /* External Windows SDK19041 header. */
#endif
#include <SDL_opengl.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include "genesis_raster_frame.h"
#include "genesis_gpu_raster_shader.h"

typedef const GLubyte *(APIENTRY *GString)(GLenum);
typedef void (APIENTRY *GInteger)(GLenum,GLint *);
typedef void (APIENTRY *GBoolean)(GLenum,GLboolean *);
typedef GLboolean (APIENTRY *GEnabled)(GLenum);
typedef void (APIENTRY *GToggle)(GLenum);
typedef GLenum (APIENTRY *GError)(void);
typedef void (APIENTRY *GViewport)(GLint,GLint,GLsizei,GLsizei);
typedef void (APIENTRY *GMask)(GLboolean,GLboolean,GLboolean,GLboolean);
typedef void (APIENTRY *GBindTexture)(GLenum,GLuint);
typedef void (APIENTRY *GTextures)(GLsizei,GLuint *);
typedef void (APIENTRY *GDeleteTextures)(GLsizei,const GLuint *);
typedef void (APIENTRY *GDraw)(GLenum,GLint,GLsizei);
typedef void (APIENTRY *GReadPixels)(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void *);
typedef void (APIENTRY *GReadBuffer)(GLenum);
typedef void (APIENTRY *GPixelStore)(GLenum,GLint);

#define GPU_PROCS(X) \
 X(GString,GetString) X(GInteger,GetIntegerv) X(GBoolean,GetBooleanv) \
 X(GEnabled,IsEnabled) X(GToggle,Enable) X(GToggle,Disable) X(GError,GetError) \
 X(GViewport,Viewport) X(GMask,ColorMask) X(GBindTexture,BindTexture) \
 X(GTextures,GenTextures) X(GDeleteTextures,DeleteTextures) X(GDraw,DrawArrays) \
 X(GReadPixels,ReadPixels) X(GReadBuffer,ReadBuffer) X(GPixelStore,PixelStorei) \
 X(PFNGLACTIVETEXTUREPROC,ActiveTexture) X(PFNGLGENBUFFERSPROC,GenBuffers) \
 X(PFNGLBINDBUFFERPROC,BindBuffer) X(PFNGLBUFFERDATAPROC,BufferData) \
 X(PFNGLDELETEBUFFERSPROC,DeleteBuffers) X(PFNGLTEXBUFFERPROC,TexBuffer) \
 X(PFNGLGENVERTEXARRAYSPROC,GenVertexArrays) X(PFNGLBINDVERTEXARRAYPROC,BindVertexArray) \
 X(PFNGLDELETEVERTEXARRAYSPROC,DeleteVertexArrays) X(PFNGLCREATESHADERPROC,CreateShader) \
 X(PFNGLSHADERSOURCEPROC,ShaderSource) X(PFNGLCOMPILESHADERPROC,CompileShader) \
 X(PFNGLGETSHADERIVPROC,GetShaderiv) X(PFNGLGETSHADERINFOLOGPROC,GetShaderInfoLog) \
 X(PFNGLDELETESHADERPROC,DeleteShader) X(PFNGLCREATEPROGRAMPROC,CreateProgram) \
 X(PFNGLATTACHSHADERPROC,AttachShader) X(PFNGLLINKPROGRAMPROC,LinkProgram) \
 X(PFNGLGETPROGRAMIVPROC,GetProgramiv) X(PFNGLGETPROGRAMINFOLOGPROC,GetProgramInfoLog) \
 X(PFNGLDELETEPROGRAMPROC,DeleteProgram) X(PFNGLUSEPROGRAMPROC,UseProgram) \
 X(PFNGLGETUNIFORMLOCATIONPROC,GetUniformLocation) X(PFNGLUNIFORM1IPROC,Uniform1i) \
 X(PFNGLGENFRAMEBUFFERSPROC,GenFramebuffers) X(PFNGLBINDFRAMEBUFFERPROC,BindFramebuffer) \
 X(PFNGLFRAMEBUFFERTEXTURE2DPROC,FramebufferTexture2D) \
 X(PFNGLCHECKFRAMEBUFFERSTATUSPROC,CheckFramebufferStatus) \
 X(PFNGLDELETEFRAMEBUFFERSPROC,DeleteFramebuffers)
typedef struct GpuApi {
#define GPU_FIELD(type,name) type name;
 GPU_PROCS(GPU_FIELD)
#undef GPU_FIELD
} GpuApi;
struct GenesisGpuRaster {
    SDL_Renderer *renderer;SDL_Texture *texture;
    SDL_Window *window;SDL_GLContext context;
    GpuApi gl;GenesisRasterFrame *frame;
    GLuint program,vao,buffer,buffer_texture,fbo;
    uint32_t *words,*preview;size_t capacity,used,preview_capacity;
    int stride,sync,ready,last_drawn,broken;
    void (*before_replace)(void *);void *user;
    uint64_t draws,upload_bytes,previews;
};
typedef struct GpuState {
    GLint program,vao,buffer,fbo,active,texture_buffer,viewport[4];
    GLboolean mask[4],enabled[8];
} GpuState;
static const GLenum gpu_flags[8]={GL_BLEND,GL_DEPTH_TEST,GL_STENCIL_TEST,GL_CULL_FACE,
    GL_SCISSOR_TEST,GL_DITHER,GL_FRAMEBUFFER_SRGB,GL_RASTERIZER_DISCARD};
static void gpu_save(GenesisGpuRaster *g,GpuState *s)
{
    GpuApi *a=&g->gl;
    a->GetIntegerv(GL_CURRENT_PROGRAM,&s->program);a->GetIntegerv(GL_VERTEX_ARRAY_BINDING,&s->vao);
    a->GetIntegerv(GL_ARRAY_BUFFER_BINDING,&s->buffer);a->GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&s->fbo);
    a->GetIntegerv(GL_ACTIVE_TEXTURE,&s->active);a->GetIntegerv(GL_VIEWPORT,s->viewport);
    a->GetBooleanv(GL_COLOR_WRITEMASK,s->mask);
    a->ActiveTexture(GL_TEXTURE1);a->GetIntegerv(GL_TEXTURE_BINDING_BUFFER,&s->texture_buffer);
    for(unsigned i=0;i<8;i++){s->enabled[i]=a->IsEnabled(gpu_flags[i]);a->Disable(gpu_flags[i]);}
    a->ColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);a->ActiveTexture(GL_TEXTURE0);
}
static void gpu_restore(GenesisGpuRaster *g,const GpuState *s)
{
    GpuApi *a=&g->gl;
    a->UseProgram((GLuint)s->program);a->BindVertexArray((GLuint)s->vao);
    a->BindBuffer(GL_ARRAY_BUFFER,(GLuint)s->buffer);a->BindFramebuffer(GL_DRAW_FRAMEBUFFER,(GLuint)s->fbo);
    a->Viewport(s->viewport[0],s->viewport[1],s->viewport[2],s->viewport[3]);
    a->ColorMask(s->mask[0],s->mask[1],s->mask[2],s->mask[3]);
    for(unsigned i=0;i<8;i++)if(s->enabled[i])a->Enable(gpu_flags[i]);else a->Disable(gpu_flags[i]);
    a->ActiveTexture(GL_TEXTURE1);a->BindTexture(GL_TEXTURE_BUFFER,(GLuint)s->texture_buffer);
    a->ActiveTexture(GL_TEXTURE0);
    /* SDL's public unbind invalidates its texture/texturing cache. Do not
     * restore a raw GL binding behind that cache after this operation. */
    if(g->texture)(void)SDL_GL_UnbindTexture(g->texture);
    a->ActiveTexture((GLenum)s->active);
}
static GLuint gpu_shader(GenesisGpuRaster *g,GLenum kind,const char *source)
{
    GLuint shader=g->gl.CreateShader(kind);if(!shader)return 0;
    g->gl.ShaderSource(shader,1,&source,NULL);g->gl.CompileShader(shader);
    GLint ok=0;g->gl.GetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[2048];GLsizei length=0;g->gl.GetShaderInfoLog(shader,sizeof log,&length,log);
        fprintf(stderr,"[GPU raster] shader failed: %.*s\n",(int)length,log);g->gl.DeleteShader(shader);return 0;}
    return shader;
}
GenesisGpuRaster *genesis_gpu_raster_create(SDL_Renderer *renderer,void (*notify)(void *),void *user)
{
    const char *mode=getenv("GENESIS_GPU_RASTER");
    if(!mode || (strcmp(mode,"tiles") && strcmp(mode,"expanded")))return NULL;
    SDL_RendererInfo info;memset(&info,0,sizeof info);
    uint32_t endian=1;
    if(!renderer || ((const uint8_t *)&endian)[0]!=1 || SDL_GetRendererInfo(renderer,&info)!=0 ||
       !info.name || strcmp(info.name,"opengl") || !(info.flags&SDL_RENDERER_ACCELERATED))return NULL;
    if(SDL_RenderFlush(renderer)!=0)return NULL;
    SDL_Texture *probe=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,1,1);
    if(!probe)return NULL;
    if(SDL_GL_BindTexture(probe,NULL,NULL)!=0){SDL_DestroyTexture(probe);return NULL;}
    SDL_Window *window=SDL_GL_GetCurrentWindow();SDL_GLContext context=SDL_GL_GetCurrentContext();
    (void)SDL_GL_UnbindTexture(probe);SDL_DestroyTexture(probe);
    if(!window || !context)return NULL;
    GenesisGpuRaster *g=(GenesisGpuRaster *)calloc(1,sizeof *g);if(!g)return NULL;
    g->renderer=renderer;g->window=window;g->context=context;
    g->before_replace=notify;g->user=user;g->sync=1;
#define GPU_LOAD(type,name) do { void *p=SDL_GL_GetProcAddress("gl" #name); \
    if(!p){free(g);return NULL;} _Static_assert(sizeof(g->gl.name)==sizeof p,"SDL GL ABI"); \
    memcpy((void *)&g->gl.name,&p,sizeof p); } while(0);
    GPU_PROCS(GPU_LOAD)
#undef GPU_LOAD
    GLint major=0,minor=0,limit=0;
    g->gl.GetIntegerv(GL_MAJOR_VERSION,&major);g->gl.GetIntegerv(GL_MINOR_VERSION,&minor);
    const char *device=(const char *)g->gl.GetString(GL_RENDERER);
    if((major<3 || (major==3 && minor<3)) || !device || strstr(device,"llvmpipe") ||
       strstr(device,"softpipe") || strstr(device,"GDI Generic") || strstr(device,"SwiftShader")) {free(g);return NULL;}
    g->gl.GetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE,&limit);
    size_t cap=1536u+GENESIS_RASTER_ROWS*16u+GENESIS_RASTER_ROWS*(7u*((GVDP_MAX_WIDTH+3u)/4u)+4u*GVDP_TILE_PACKET_SPANS+32u);
    if(limit<=0 || (size_t)limit<1536u+GENESIS_RASTER_ROWS*16u){free(g);return NULL;}
    if(cap>(size_t)limit)cap=(size_t)limit;
    g->frame=(GenesisRasterFrame *)calloc(1,sizeof *g->frame);g->words=(uint32_t *)malloc(cap*sizeof(uint32_t));
    if(!g->frame || !g->words){free(g->frame);free(g->words);free(g);return NULL;}
    g->frame->compact=!strcmp(mode,"tiles");g->capacity=cap;
    GLuint vs=gpu_shader(g,GL_VERTEX_SHADER,genesis_gpu_vertex),fs=gpu_shader(g,GL_FRAGMENT_SHADER,genesis_gpu_fragment);
    if(vs && fs){g->program=g->gl.CreateProgram();g->gl.AttachShader(g->program,vs);g->gl.AttachShader(g->program,fs);g->gl.LinkProgram(g->program);}
    if(vs)g->gl.DeleteShader(vs);if(fs)g->gl.DeleteShader(fs);
    GLint linked=0;if(g->program)g->gl.GetProgramiv(g->program,GL_LINK_STATUS,&linked);
    if(!linked){if(g->program){char log[2048];GLsizei n=0;g->gl.GetProgramInfoLog(g->program,sizeof log,&n,log);fprintf(stderr,"[GPU raster] link failed: %.*s\n",(int)n,log);g->gl.DeleteProgram(g->program);}free(g->frame);free(g->words);free(g);return NULL;}
    GLint previous=0;g->gl.GetIntegerv(GL_CURRENT_PROGRAM,&previous);g->gl.UseProgram(g->program);
    g->gl.Uniform1i(g->gl.GetUniformLocation(g->program,"data"),1);
    g->gl.Uniform1i(g->gl.GetUniformLocation(g->program,"rows"),GENESIS_RASTER_ROWS);
    g->gl.UseProgram((GLuint)previous);
    g->gl.GenVertexArrays(1,&g->vao);g->gl.GenBuffers(1,&g->buffer);g->gl.GenTextures(1,&g->buffer_texture);g->gl.GenFramebuffers(1,&g->fbo);
    if(!g->vao || !g->buffer || !g->buffer_texture || !g->fbo){genesis_gpu_raster_destroy(g);return NULL;}
    fprintf(stderr,"[GPU raster] mode=%s device=%s GL=%s; raw VDP only, portable CPU fallback\n",mode,device,(const char *)g->gl.GetString(GL_VERSION));
    return g;
}
void genesis_gpu_raster_resolve(GenesisGpuRaster *g){if(g)genesis_raster_frame_resolve(g->frame);}
void genesis_gpu_raster_invalidate(GenesisGpuRaster *g)
{
    if(!g)return;if(g->before_replace)g->before_replace(g->user);
    genesis_raster_frame_detach(g->frame);g->ready=0;g->sync=1;
}
void genesis_gpu_raster_destroy(GenesisGpuRaster *g)
{
    if(!g)return;genesis_gpu_raster_invalidate(g);
    (void)SDL_GL_MakeCurrent(g->window,g->context);
    fprintf(stderr,"[GPU raster] draws=%llu captured_rows=%llu materialized_rows=%llu upload_bytes=%llu previews=%llu\n",
        (unsigned long long)g->draws,(unsigned long long)g->frame->captures,(unsigned long long)g->frame->resolves,
        (unsigned long long)g->upload_bytes,(unsigned long long)g->previews);
    if(g->texture)SDL_DestroyTexture(g->texture);
    if(g->fbo)g->gl.DeleteFramebuffers(1,&g->fbo);if(g->buffer_texture)g->gl.DeleteTextures(1,&g->buffer_texture);
    if(g->buffer)g->gl.DeleteBuffers(1,&g->buffer);if(g->vao)g->gl.DeleteVertexArrays(1,&g->vao);
    if(g->program)g->gl.DeleteProgram(g->program);free(g->preview);free(g->words);free(g->frame);free(g);
}
int genesis_gpu_raster_begin(GenesisGpuRaster *g,uint32_t *raw,int stride,uint64_t generation,int eligible)
{
    if(!g)return 0;
    if(!raw || stride<GVDP_MAX_WIDTH || stride>0x1fffffff){genesis_raster_frame_abort(g->frame);g->ready=0;return 0;}
    if(!eligible || g->broken){genesis_raster_frame_abort(g->frame);g->ready=0;g->last_drawn=0;g->sync=1;return 0;}
    if(g->stride!=stride || g->frame->raw!=raw || g->frame->generation!=generation)g->sync=1;
    if(g->sync){
        if(g->before_replace)g->before_replace(g->user);
        genesis_raster_frame_resolve(g->frame);
        if(g->stride!=stride || !g->texture){
            if(g->texture)SDL_DestroyTexture(g->texture);g->texture=NULL;
            g->texture=SDL_CreateTexture(g->renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,stride,GENESIS_RASTER_ROWS);
            if(!g->texture){g->broken=1;return 0;}g->stride=stride;
        }
        if(SDL_UpdateTexture(g->texture,NULL,raw,stride*4)!=0){g->broken=1;return 0;}
        float tw=0,th=0;
        if(SDL_GL_BindTexture(g->texture,&tw,&th)!=0){g->broken=1;return 0;}
        (void)SDL_GL_UnbindTexture(g->texture);
        /* SDL's rectangle/power-of-two compatibility layouts are supported by
         * the original CPU renderer. Do not assume their backing dimensions. */
        if(tw!=1.0f || th!=1.0f){g->broken=1;return 0;}
        g->sync=0;
    }
    g->ready=genesis_raster_frame_begin(g->frame,raw,stride,generation);return g->ready;
}
int genesis_gpu_raster_capture(GenesisGpuRaster *g,GVDP *v,int line)
{
    if(!g || !g->ready)return 0;
    int width=genesis_raster_frame_capture(g->frame,v,line);
    if(!width){genesis_raster_frame_abort(g->frame);g->ready=0;g->sync=1;}
    return width;
}
static int gpu_pack(GenesisGpuRaster *g)
{
    size_t top=1536u+GENESIS_RASTER_ROWS*16u;
    memcpy(g->words,genesis_dac_colors,1536u*sizeof(uint32_t));
    memset(g->words+1536,0,GENESIS_RASTER_ROWS*16u*sizeof(uint32_t));
    for(unsigned y=0;y<GENESIS_RASTER_ROWS;y++)if(g->frame->captured[y]) {
        const GenesisRasterRow *p=&g->frame->rows[g->frame->slot[y]][y];const GVDPRasterRow *r=&p->packet.raster;
        unsigned lane=(r->width+3u)/4u;
        size_t extra=7u*lane+32u+(g->frame->compact?2u*(p->packet.span_count[0]+p->packet.span_count[1]):0u);
        if(extra>g->capacity-top)return 0;
        uint32_t *h=g->words+1536+y*16;h[0]=r->width;h[1]=r->start;h[2]=r->count;
        h[3]=r->backdrop;h[4]=r->bar;h[5]=r->display;h[6]=r->shadow_mode;
        h[7]=r->diagnostic;h[8]=r->diag_offset;h[9]=r->diag_mask;h[10]=lane;h[11]=(uint32_t)top;h[15]=(uint32_t)g->frame->compact;
        const uint8_t *sources[7]={g->frame->compact?p->packet.map[0]:r->plane_a,
            g->frame->compact?p->packet.map[1]:r->plane_b,r->sprite,r->opaque,r->high,r->shadow_op,r->hilite_op};
        for(unsigned k=0;k<7;k++){memset(g->words+top,0,lane*4u);memcpy(g->words+top,sources[k],r->width);top+=lane;}
        for(unsigned plane=0;plane<2;plane++){
            h[12+plane]=(uint32_t)top;
            if(g->frame->compact)for(unsigned j=0;j<p->packet.span_count[plane];j++){
                g->words[top++]=p->packet.spans[plane][j].bits;g->words[top++]=p->packet.spans[plane][j].meta;
            }
        }
        h[14]=(uint32_t)top;
        for(unsigned k=0;k<32;k++)g->words[top++]=(uint32_t)p->cram[k*2]|((uint32_t)p->cram[k*2+1]<<16);
    }
    g->used=top;return 1;
}
SDL_Texture *genesis_gpu_raster_draw(GenesisGpuRaster *g)
{
    if(!g || !g->ready || !g->texture)return NULL;
    if(!gpu_pack(g) || SDL_RenderFlush(g->renderer)!=0 || SDL_GL_MakeCurrent(g->window,g->context)!=0)goto fail;
    GpuState state;gpu_save(g,&state);
    int ok=SDL_GL_BindTexture(g->texture,NULL,NULL)==0;GLint target=0;
    if(ok)g->gl.GetIntegerv(GL_TEXTURE_BINDING_2D,&target);
    if(!target)ok=0;
    if(ok){
        g->gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER,g->fbo);
        g->gl.FramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,(GLuint)target,0);
        ok=g->gl.CheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
    }
    if(ok){
        g->gl.BindBuffer(GL_ARRAY_BUFFER,g->buffer);g->gl.BufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(g->used*4u),g->words,GL_STREAM_DRAW);
        g->gl.ActiveTexture(GL_TEXTURE1);g->gl.BindTexture(GL_TEXTURE_BUFFER,g->buffer_texture);g->gl.TexBuffer(GL_TEXTURE_BUFFER,GL_R32UI,g->buffer);
        g->gl.UseProgram(g->program);g->gl.BindVertexArray(g->vao);g->gl.Viewport(0,0,g->stride,GENESIS_RASTER_ROWS);
        g->gl.DrawArrays(GL_TRIANGLES,0,3);ok=g->gl.GetError()==GL_NO_ERROR;
    }
    gpu_restore(g,&state);
    if(!ok)goto fail;
    ++g->draws;g->upload_bytes+=g->used*4u;g->last_drawn=1;return g->texture;
fail:
    genesis_raster_frame_abort(g->frame);g->ready=0;g->sync=1;g->last_drawn=0;g->broken=1;
    fprintf(stderr,"[GPU raster] resource/dispatch unavailable; using complete CPU output\n");return NULL;
}
SDL_Texture *genesis_gpu_raster_last_texture(GenesisGpuRaster *g){return g && g->last_drawn?g->texture:NULL;}
int genesis_gpu_raster_preview(void *ctx,uint32_t *out,int width,int height,int sw,int sh)
{
    GenesisGpuRaster *g=(GenesisGpuRaster *)ctx;
    if(!g || !g->texture || !out || width<=0 || height<=0 || sw<=0 || sh<=0 || sw>g->stride || sh>GENESIS_RASTER_ROWS)return 0;
    size_t capacity=(size_t)g->stride*GENESIS_RASTER_ROWS;
    if(g->preview_capacity<capacity){uint32_t *p=(uint32_t *)realloc(g->preview,capacity*4u);if(!p)return 0;g->preview=p;g->preview_capacity=capacity;}
    if(SDL_RenderFlush(g->renderer)!=0 || SDL_GL_MakeCurrent(g->window,g->context)!=0)return 0;
    GpuApi *a=&g->gl;GLint active=0,pack=0,alignment=0,row_length=0,skip_rows=0,skip_pixels=0;
    GLint previous_read=0,target=0;
    a->GetIntegerv(GL_ACTIVE_TEXTURE,&active);a->ActiveTexture(GL_TEXTURE0);
    if(SDL_GL_BindTexture(g->texture,NULL,NULL)!=0){a->ActiveTexture((GLenum)active);return 0;}
    a->GetIntegerv(GL_TEXTURE_BINDING_2D,&target);a->GetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&previous_read);
    a->BindFramebuffer(GL_READ_FRAMEBUFFER,g->fbo);
    a->FramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,(GLuint)target,0);
    if(!target || a->CheckFramebufferStatus(GL_READ_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){
        a->BindFramebuffer(GL_READ_FRAMEBUFFER,(GLuint)previous_read);
        (void)SDL_GL_UnbindTexture(g->texture);a->ActiveTexture((GLenum)active);return 0;
    }
    a->ReadBuffer(GL_COLOR_ATTACHMENT0);
    a->GetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&pack);a->GetIntegerv(GL_PACK_ALIGNMENT,&alignment);
    a->GetIntegerv(GL_PACK_ROW_LENGTH,&row_length);a->GetIntegerv(GL_PACK_SKIP_ROWS,&skip_rows);a->GetIntegerv(GL_PACK_SKIP_PIXELS,&skip_pixels);
    a->BindBuffer(GL_PIXEL_PACK_BUFFER,0);a->PixelStorei(GL_PACK_ALIGNMENT,4);a->PixelStorei(GL_PACK_ROW_LENGTH,0);
    a->PixelStorei(GL_PACK_SKIP_ROWS,0);a->PixelStorei(GL_PACK_SKIP_PIXELS,0);
    /* Bounded logical rectangle, never the possibly larger native backing. */
    a->ReadPixels(0,0,g->stride,GENESIS_RASTER_ROWS,GL_BGRA,GL_UNSIGNED_BYTE,g->preview);
    int ok=a->GetError()==GL_NO_ERROR;
    a->PixelStorei(GL_PACK_ALIGNMENT,alignment);a->PixelStorei(GL_PACK_ROW_LENGTH,row_length);
    a->PixelStorei(GL_PACK_SKIP_ROWS,skip_rows);a->PixelStorei(GL_PACK_SKIP_PIXELS,skip_pixels);
    a->BindBuffer(GL_PIXEL_PACK_BUFFER,(GLuint)pack);a->BindFramebuffer(GL_READ_FRAMEBUFFER,(GLuint)previous_read);
    (void)SDL_GL_UnbindTexture(g->texture);a->ActiveTexture((GLenum)active);
    if(!ok)return 0;
    for(int y=0;y<height;y++)for(int x=0;x<width;x++)out[(size_t)y*(size_t)width+x]=
        g->preview[(size_t)(y*sh/height)*(size_t)g->stride+x*sw/width]|0xff000000u;
    ++g->previews;return 1;
}
#else
struct GenesisGpuRaster { int unavailable; };
GenesisGpuRaster *genesis_gpu_raster_create(SDL_Renderer *r,void (*f)(void *),void *u){(void)r;(void)f;(void)u;return NULL;}
void genesis_gpu_raster_destroy(GenesisGpuRaster *g){(void)g;}
int genesis_gpu_raster_begin(GenesisGpuRaster *g,uint32_t *r,int s,uint64_t n,int e){(void)g;(void)r;(void)s;(void)n;(void)e;return 0;}
int genesis_gpu_raster_capture(GenesisGpuRaster *g,GVDP *v,int l){(void)g;(void)v;(void)l;return 0;}
void genesis_gpu_raster_resolve(GenesisGpuRaster *g){(void)g;}
void genesis_gpu_raster_invalidate(GenesisGpuRaster *g){(void)g;}
SDL_Texture *genesis_gpu_raster_draw(GenesisGpuRaster *g){(void)g;return NULL;}
SDL_Texture *genesis_gpu_raster_last_texture(GenesisGpuRaster *g){(void)g;return NULL;}
int genesis_gpu_raster_preview(void *g,uint32_t *o,int w,int h,int sw,int sh){(void)g;(void)o;(void)w;(void)h;(void)sw;(void)sh;return 0;}
#endif
