/* Presentation of immutable hardware raster jobs. Native rendering, status,
 * reads and serialization do not depend on this optional SDL/OpenGL path. */
#include "genesis_hd_gpu.h"
#include "genesis_raster_jobs.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

unsigned genesis_hd_scale(unsigned configured)
{
    if(configured<1 || configured>64)configured=1;
    const char *s=getenv("GENESIS_INTERNAL_SCALE");
    if(s&&*s){char *end=NULL;unsigned long n=strtoul(s,&end,10);
        if(end&&!*end&&n>=1&&n<=64)configured=(unsigned)n;}
    return configured;
}
int genesis_hd_prefer_renderer(unsigned scale)
{
    return (scale>1||getenv("GENESIS_INTERNAL_SCALE")) && !getenv("SDL_RENDER_DRIVER") &&
        SDL_SetHintWithPriority(SDL_HINT_RENDER_DRIVER,"opengl",SDL_HINT_DEFAULT);
}
SDL_Renderer *genesis_hd_create_renderer(SDL_Window *window,unsigned flags,unsigned scale)
{
    const char *prior=SDL_GetHint(SDL_HINT_RENDER_DRIVER);char *saved=NULL;
    if(prior){size_t n=strlen(prior)+1;saved=(char*)malloc(n);if(saved)memcpy(saved,prior,n);}
    /* Allocation failure leaves caller policy alone instead of losing it. */
    int preferred=(!prior||saved)&&genesis_hd_prefer_renderer(scale);
    SDL_Renderer *renderer=SDL_CreateRenderer(window,-1,flags);
    if(preferred){if(saved)SDL_SetHintWithPriority(SDL_HINT_RENDER_DRIVER,saved,SDL_HINT_DEFAULT);else SDL_ResetHint(SDL_HINT_RENDER_DRIVER);
        if(!renderer)renderer=SDL_CreateRenderer(window,-1,flags);}
    free(saved);return renderer;
}

#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
#include <SDL2/SDL_opengl.h>
#include "../png_write.h"
#define HD_TEX_WIDTH 1024u
#define HD_QUERY_SLOTS 8u
#define HD_MAX_TARGET_BYTES (UINT64_C(512)*1024*1024)
#define HD_MAX_SAMPLES 4096u

#define GL_API(X) \
 X(const GLubyte*,GetString,(GLenum)) \
 X(void,GetIntegerv,(GLenum,GLint*)) X(void,GetBooleanv,(GLenum,GLboolean*)) \
 X(GLboolean,IsEnabled,(GLenum)) X(GLenum,GetError,(void)) \
 X(void,Enable,(GLenum)) X(void,Disable,(GLenum)) \
 X(void,Viewport,(GLint,GLint,GLsizei,GLsizei)) X(void,Scissor,(GLint,GLint,GLsizei,GLsizei)) \
 X(void,ColorMask,(GLboolean,GLboolean,GLboolean,GLboolean)) \
 X(void,PixelStorei,(GLenum,GLint)) X(void,ActiveTexture,(GLenum)) \
 X(void,GenTextures,(GLsizei,GLuint*)) X(void,DeleteTextures,(GLsizei,const GLuint*)) \
 X(void,BindTexture,(GLenum,GLuint)) X(void,TexParameteri,(GLenum,GLenum,GLint)) \
 X(void,TexImage2D,(GLenum,GLint,GLint,GLsizei,GLsizei,GLint,GLenum,GLenum,const void*)) \
 X(void,TexSubImage2D,(GLenum,GLint,GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,const void*)) \
 X(GLuint,CreateShader,(GLenum)) X(void,ShaderSource,(GLuint,GLsizei,const GLchar*const*,const GLint*)) \
 X(void,CompileShader,(GLuint)) X(void,GetShaderiv,(GLuint,GLenum,GLint*)) \
 X(void,GetShaderInfoLog,(GLuint,GLsizei,GLsizei*,GLchar*)) X(void,DeleteShader,(GLuint)) \
 X(GLuint,CreateProgram,(void)) X(void,AttachShader,(GLuint,GLuint)) X(void,LinkProgram,(GLuint)) \
 X(void,BindFragDataLocation,(GLuint,GLuint,const GLchar*)) \
 X(void,GetProgramiv,(GLuint,GLenum,GLint*)) X(void,GetProgramInfoLog,(GLuint,GLsizei,GLsizei*,GLchar*)) \
 X(void,UseProgram,(GLuint)) X(void,DeleteProgram,(GLuint)) \
 X(GLint,GetUniformLocation,(GLuint,const GLchar*)) X(void,Uniform1i,(GLint,GLint)) \
 X(void,Uniform2i,(GLint,GLint,GLint)) \
 X(void,GenVertexArrays,(GLsizei,GLuint*)) X(void,DeleteVertexArrays,(GLsizei,const GLuint*)) \
 X(void,BindVertexArray,(GLuint)) X(void,BindBuffer,(GLenum,GLuint)) \
 X(void,DrawArrays,(GLenum,GLint,GLsizei))
typedef struct HdGl {
#define DECL(ret,name,args) ret (APIENTRY *name) args;
 GL_API(DECL)
#undef DECL
 void (APIENTRY *GenQueries)(GLsizei,GLuint*);
 void (APIENTRY *DeleteQueries)(GLsizei,const GLuint*);
 void (APIENTRY *QueryCounter)(GLuint,GLenum);
 void (APIENTRY *GetQueryObjectiv)(GLuint,GLenum,GLint*);
 void (APIENTRY *GetQueryObjectui64v)(GLuint,GLenum,GLuint64*);
} HdGl;
typedef struct HdTexture {
    GLuint name;
    uint32_t *shadow;
    unsigned width,height,components;
} HdTexture;
typedef struct HdSample {
    uint64_t serial,upload_ns,draw_ns,upload_bytes,cpu_ticks;
    unsigned scale,width,height;
} HdSample;
struct GenesisHdGpu {
    SDL_Renderer *renderer;
    SDL_GLContext context;
    SDL_Texture *output;
    HdGl gl;
    GLuint program,vao,query[HD_QUERY_SLOTS][3];
    unsigned pending[HD_QUERY_SLOTS],query_cursor;
    uint64_t query_serial[HD_QUERY_SLOTS],query_bytes[HD_QUERY_SLOTS],query_cpu[HD_QUERY_SLOTS];
    unsigned query_scale[HD_QUERY_SLOTS],query_width[HD_QUERY_SLOTS],query_height[HD_QUERY_SLOTS];
    HdTexture texture[5];
    unsigned max_texture,width,height,requested_w,requested_h;
    int failed,timers,last_valid,last_graded;
    uint64_t last_serial;
    ColorLut last_lut;
    uint8_t pattern_valid[GVDP_HD_PATTERNS],palette_valid[GVDP_HD_PALETTES];
    GenesisHdGpuMetrics metrics;
    HdSample *samples;
    unsigned sample_count;
};
typedef struct HdState {
    GLint program,active,vao,unpack_buffer,viewport[4],scissor[4],textures[5],unpack[6];
    GLboolean mask[4],enabled[8];
} HdState;
static const GLenum switches[]={GL_BLEND,GL_SCISSOR_TEST,GL_DEPTH_TEST,GL_STENCIL_TEST,
    GL_CULL_FACE,GL_DITHER,GL_FRAMEBUFFER_SRGB,GL_RASTERIZER_DISCARD};
static const GLenum stores[]={GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,GL_UNPACK_SKIP_PIXELS,
    GL_UNPACK_SKIP_ROWS,GL_UNPACK_SWAP_BYTES,GL_UNPACK_LSB_FIRST};

static const char vertex_source[]=
"#version 130\n"
"void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0.0,1.0);}\n";
static const char fragment_source[]=
"#version 130\n"
"uniform usampler2D rows_tex,spans_tex,patterns_tex,palettes_tex,operators_tex;\n"
"uniform ivec2 native_size,output_size;out vec4 result;\n"
"uvec4 row_at(uint id){return texelFetch(rows_tex,ivec2(int(id&1023u),int(id>>10)),0);}\n"
"uvec4 span_at(uint id){return texelFetch(spans_tex,ivec2(int(id&1023u),int(id>>10)),0);}\n"
"uint pattern_at(uint id){return texelFetch(patterns_tex,ivec2(int(id&1023u),int(id>>10)),0).r;}\n"
"uint ops_at(uint id){return texelFetch(operators_tex,ivec2(int(id&1023u),int(id>>10)),0).r;}\n"
"uint upper_bound(uint first,uint count,uint x){uint lo=0u,hi=count;for(int k=0;k<20&&lo<hi;++k){uint m=(lo+hi)>>1;if(span_at(first+m).x<=x)lo=m+1u;else hi=m;}return lo;}\n"
"uvec3 sample_span(uvec4 s,uint x,bool sprite){uint k=x-s.x,n=s.z&255u;if(k>=n)return uvec3(0u);if(sprite&&((s.w>>(16u+k))&1u)==0u)return uvec3(0u);uint q=((s.z>>8)&255u)+k;if((s.z&65536u)!=0u)q^=7u;if(q>7u)return uvec3(0u);uint nib=(pattern_at(s.y)>>(28u-q*4u))&15u;if(nib==0u)return uvec3(0u);return uvec3((s.w&255u)+nib,1u,(s.w>>8)&1u);}\n"
"uvec3 plane(uint first,uint count,uint x){uint p=upper_bound(first,count,x);if(p==0u)return uvec3(0u);return sample_span(span_at(first+p-1u),x,false);}\n"
"uvec3 sprite(uint first,uint count,uint x){uint p=upper_bound(first,count,x);for(int n=0;n<15&&p>0u;++n){uvec4 s=span_at(first+--p);if(x-s.x>7u)break;uvec3 c=sample_span(s,x,true);if(c.y!=0u)return c;}return uvec3(0u);}\n"
"void main(){ivec2 p=ivec2(gl_FragCoord.xy)*native_size/output_size;uint x=uint(p.x),y=uint(p.y);uvec4 r0=row_at(y*5u),r1=row_at(y*5u+1u),r2=row_at(y*5u+2u),r3=row_at(y*5u+3u),r4=row_at(y*5u+4u);uint c=r1.x;\n"
"if(x>=r0.y&&x-r0.y<r0.z){c=r0.w;if(r4.y!=0u){uvec3 s=sprite(r3.z,r3.w,x);bool high=s.y!=0u&&s.z!=0u;\n"
"if(high)c=s.x;else{uvec3 a=plane(r2.z,r2.w,x);if(a.y!=0u&&a.z!=0u){c=a.x;high=true;}else{uvec3 b=plane(r3.x,r3.y,x);if(b.y!=0u&&b.z!=0u){c=b.x;high=true;}else if(s.y!=0u)c=s.x;else if(a.y!=0u)c=a.x;else if(b.y!=0u)c=b.x;}}\n"
"if(r1.y!=0u&&c<64u){uint o=ops_at(r4.x+x/8u);bool shadow=!high,highlight=false;if(((o>>(x&7u))&1u)!=0u)shadow=true;if(((o>>(8u+(x&7u)))&1u)!=0u){if(shadow)shadow=false;else highlight=true;}if(highlight)c+=128u;else if(shadow)c+=64u;}\n"
"if(r1.z!=0u&&c<193u&&((x-r1.w)&r2.x)==0u)c=(c&63u)+64u;}}\n"
"uint argb=texelFetch(palettes_tex,ivec2(int(c),int(r2.y)),0).r;result=vec4(float((argb>>16)&255u),float((argb>>8)&255u),float(argb&255u),float((argb>>24)&255u))/255.0;}\n";

static int load_gl(HdGl *g)
{
#define LOAD(ret,name,args) g->name=(ret(APIENTRY*)args)SDL_GL_GetProcAddress("gl" #name);if(!g->name)return 0;
    GL_API(LOAD)
#undef LOAD
    g->GenQueries=(void(APIENTRY*)(GLsizei,GLuint*))SDL_GL_GetProcAddress("glGenQueries");
    g->DeleteQueries=(void(APIENTRY*)(GLsizei,const GLuint*))SDL_GL_GetProcAddress("glDeleteQueries");
    g->QueryCounter=(void(APIENTRY*)(GLuint,GLenum))SDL_GL_GetProcAddress("glQueryCounter");
    g->GetQueryObjectiv=(void(APIENTRY*)(GLuint,GLenum,GLint*))SDL_GL_GetProcAddress("glGetQueryObjectiv");
    g->GetQueryObjectui64v=(void(APIENTRY*)(GLuint,GLenum,GLuint64*))SDL_GL_GetProcAddress("glGetQueryObjectui64v");
    return 1;
}
static void state_begin(HdGl *g,HdState *s)
{
    g->GetIntegerv(GL_CURRENT_PROGRAM,&s->program);g->GetIntegerv(GL_ACTIVE_TEXTURE,&s->active);
    g->GetIntegerv(GL_VERTEX_ARRAY_BINDING,&s->vao);g->GetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&s->unpack_buffer);
    g->GetIntegerv(GL_VIEWPORT,s->viewport);g->GetIntegerv(GL_SCISSOR_BOX,s->scissor);g->GetBooleanv(GL_COLOR_WRITEMASK,s->mask);
    for(unsigned i=0;i<8;++i){s->enabled[i]=g->IsEnabled(switches[i]);g->Disable(switches[i]);}
    for(unsigned i=0;i<6;++i)g->GetIntegerv(stores[i],&s->unpack[i]);
    for(unsigned i=0;i<5;++i){g->ActiveTexture(GL_TEXTURE0+i);g->GetIntegerv(GL_TEXTURE_BINDING_2D,&s->textures[i]);}
    g->BindBuffer(GL_PIXEL_UNPACK_BUFFER,0);g->PixelStorei(GL_UNPACK_ALIGNMENT,1);
    for(unsigned i=1;i<6;++i)g->PixelStorei(stores[i],0);
    g->ColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
}
static void state_end(HdGl *g,const HdState *s)
{
    g->UseProgram((GLuint)s->program);g->BindVertexArray((GLuint)s->vao);g->BindBuffer(GL_PIXEL_UNPACK_BUFFER,(GLuint)s->unpack_buffer);
    for(unsigned i=0;i<6;++i)g->PixelStorei(stores[i],s->unpack[i]);
    for(unsigned i=0;i<5;++i){g->ActiveTexture(GL_TEXTURE0+i);g->BindTexture(GL_TEXTURE_2D,(GLuint)s->textures[i]);}
    g->ActiveTexture((GLenum)s->active);g->Viewport(s->viewport[0],s->viewport[1],s->viewport[2],s->viewport[3]);
    g->Scissor(s->scissor[0],s->scissor[1],s->scissor[2],s->scissor[3]);g->ColorMask(s->mask[0],s->mask[1],s->mask[2],s->mask[3]);
    for(unsigned i=0;i<8;++i){if(s->enabled[i])g->Enable(switches[i]);else g->Disable(switches[i]);}
}
static GLuint compile_shader(HdGl *g,GLenum type,const char *source)
{
    GLuint sh=g->CreateShader(type);if(!sh)return 0;g->ShaderSource(sh,1,&source,NULL);g->CompileShader(sh);
    GLint ok=0;g->GetShaderiv(sh,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[2048]={0};g->GetShaderInfoLog(sh,sizeof log,NULL,log);fprintf(stderr,"[HD_GPU] shader failed: %s\n",log);g->DeleteShader(sh);return 0;}
    return sh;
}
static void bind_texture(GenesisHdGpu *s,unsigned unit)
{s->gl.ActiveTexture(GL_TEXTURE0+unit);s->gl.BindTexture(GL_TEXTURE_2D,s->texture[unit].name);}
static int texture_storage(GenesisHdGpu *s,unsigned unit,unsigned pixels,unsigned components,unsigned fixed_width)
{
    HdTexture *t=&s->texture[unit];unsigned width=fixed_width?fixed_width:HD_TEX_WIDTH,height=1;
    while((uint64_t)width*height<pixels){if(height>UINT_MAX/2)return 0;height*=2;}
    if(width>s->max_texture||height>s->max_texture||components>4||!components||
       (uint64_t)width*height*components>UINT64_C(16)*1024*1024)return 0;
    if(t->shadow&&t->width==width&&t->height>=height&&t->components==components)return 1;
    size_t words=(size_t)width*height*components;uint32_t *copy=(uint32_t*)calloc(words,sizeof *copy);if(!copy)return 0;
    free(t->shadow);t->shadow=copy;t->width=width;t->height=height;t->components=components;
    bind_texture(s,unit);s->gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);s->gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    s->gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);s->gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    s->gl.TexImage2D(GL_TEXTURE_2D,0,components==4?GL_RGBA32UI:GL_R32UI,(GLsizei)width,(GLsizei)height,0,components==4?GL_RGBA_INTEGER:GL_RED_INTEGER,GL_UNSIGNED_INT,copy);
    s->metrics.uploaded_bytes+=(uint64_t)words*4;
    if(unit==2||unit==3)s->metrics.source_upload_bytes+=(uint64_t)words*4;else s->metrics.job_upload_bytes+=(uint64_t)words*4;
    return 1;
}
static void upload_range(GenesisHdGpu *s,unsigned unit,unsigned first,unsigned count)
{
    HdTexture *t=&s->texture[unit];bind_texture(s,unit);
    s->gl.TexSubImage2D(GL_TEXTURE_2D,0,0,(GLint)first,(GLsizei)t->width,(GLsizei)count,t->components==4?GL_RGBA_INTEGER:GL_RED_INTEGER,GL_UNSIGNED_INT,t->shadow+(size_t)first*t->width*t->components);
    uint64_t bytes=(uint64_t)t->width*count*t->components*4;s->metrics.uploaded_bytes+=bytes;
    if(unit==2||unit==3)s->metrics.source_upload_bytes+=bytes;else s->metrics.job_upload_bytes+=bytes;
}
static int upload_jobs(GenesisHdGpu *s,unsigned unit,const uint32_t *words,unsigned word_count,unsigned components)
{
    if(!texture_storage(s,unit,(word_count+components-1)/components,components,0))return 0;
    HdTexture *t=&s->texture[unit];unsigned row_words=t->width*components,rows=(word_count+row_words-1)/row_words,first=UINT_MAX;
    for(unsigned y=0;y<rows;++y){unsigned at=y*row_words,n=word_count-at;if(n>row_words)n=row_words;
        int dirty=memcmp(t->shadow+at,words+at,(size_t)n*4)!=0;
        if(dirty){memcpy(t->shadow+at,words+at,(size_t)n*4);if(first==UINT_MAX)first=y;}
        else if(first!=UINT_MAX){upload_range(s,unit,first,y-first);first=UINT_MAX;}}
    if(first!=UINT_MAX)upload_range(s,unit,first,rows-first);
    return 1;
}
static void collect_query(GenesisHdGpu *s,unsigned slot)
{
    if(!s->pending[slot])return;
    GLint ready=0;s->gl.GetQueryObjectiv(s->query[slot][2],GL_QUERY_RESULT_AVAILABLE,&ready);if(!ready)return;
    GLuint64 a=0,b=0,c=0;s->gl.GetQueryObjectui64v(s->query[slot][0],GL_QUERY_RESULT,&a);s->gl.GetQueryObjectui64v(s->query[slot][1],GL_QUERY_RESULT,&b);s->gl.GetQueryObjectui64v(s->query[slot][2],GL_QUERY_RESULT,&c);
    s->pending[slot]=0;if(b<a||c<b)return;s->metrics.gpu_upload_ns+=b-a;s->metrics.gpu_draw_ns+=c-b;++s->metrics.gpu_samples;
    if(s->samples&&s->sample_count<HD_MAX_SAMPLES){HdSample *p=&s->samples[s->sample_count++];p->serial=s->query_serial[slot];p->upload_ns=b-a;p->draw_ns=c-b;p->upload_bytes=s->query_bytes[slot];p->cpu_ticks=s->query_cpu[slot];p->scale=s->query_scale[slot];p->width=s->query_width[slot];p->height=s->query_height[slot];}
}
GenesisHdGpu *genesis_hd_gpu_create(SDL_Renderer *renderer,SDL_Texture *activation)
{
    SDL_RendererInfo info={0};if(!renderer||!activation||SDL_GetRendererInfo(renderer,&info)||!info.name||strcmp(info.name,"opengl")||!(info.flags&SDL_RENDERER_TARGETTEXTURE))return NULL;
    if(SDL_GL_BindTexture(activation,NULL,NULL))return NULL;
    SDL_GL_UnbindTexture(activation);
    GenesisHdGpu *s=(GenesisHdGpu*)calloc(1,sizeof *s);if(!s)return NULL;s->renderer=renderer;
    if(!load_gl(&s->gl)){free(s);return NULL;}s->context=SDL_GL_GetCurrentContext();
    const char *version=(const char*)s->gl.GetString(GL_VERSION);int major=0,minor=0;if(!version||sscanf(version,"%d.%d",&major,&minor)!=2||major<3){free(s);return NULL;}
    GLint limit=0,units=0;s->gl.GetIntegerv(GL_MAX_TEXTURE_SIZE,&limit);s->gl.GetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,&units);
    if(limit<1024||units<5){free(s);return NULL;}s->max_texture=(unsigned)limit;
    HdState state;state_begin(&s->gl,&state);GLuint vertex=compile_shader(&s->gl,GL_VERTEX_SHADER,vertex_source),fragment=compile_shader(&s->gl,GL_FRAGMENT_SHADER,fragment_source);
    if(vertex&&fragment){s->program=s->gl.CreateProgram();s->gl.AttachShader(s->program,vertex);s->gl.AttachShader(s->program,fragment);s->gl.BindFragDataLocation(s->program,0,"result");s->gl.LinkProgram(s->program);GLint linked=0;s->gl.GetProgramiv(s->program,GL_LINK_STATUS,&linked);if(!linked){char log[2048]={0};s->gl.GetProgramInfoLog(s->program,sizeof log,NULL,log);fprintf(stderr,"[HD_GPU] link failed: %s\n",log);s->gl.DeleteProgram(s->program);s->program=0;}}
    if(vertex)s->gl.DeleteShader(vertex);
    if(fragment)s->gl.DeleteShader(fragment);
    if(s->program){GLuint names[5];s->gl.GenTextures(5,names);for(unsigned i=0;i<5;++i)s->texture[i].name=names[i];s->gl.GenVertexArrays(1,&s->vao);}
    state_end(&s->gl,&state);
    if(!s->program){free(s);return NULL;}
    s->timers=(major>3||(major==3&&minor>=3)||SDL_GL_ExtensionSupported("GL_ARB_timer_query"))&&
        s->gl.GenQueries&&s->gl.DeleteQueries&&s->gl.QueryCounter&&s->gl.GetQueryObjectiv&&s->gl.GetQueryObjectui64v;
    if(s->timers)s->gl.GenQueries(HD_QUERY_SLOTS*3,&s->query[0][0]);
    s->metrics.timestamps_available=s->timers;
    if(getenv("GENESIS_HD_TIMINGS"))s->samples=(HdSample*)calloc(HD_MAX_SAMPLES,sizeof *s->samples);
    fprintf(stderr,"[HD_GPU] backend=opengl version=%s max_texture=%u timestamps=%d\n",version,s->max_texture,s->timers);return s;
}
void genesis_hd_gpu_invalidate(GenesisHdGpu *s)
{
    if(!s)return;
    s->last_valid=0;
    memset(s->pattern_valid,0,sizeof s->pattern_valid);memset(s->palette_valid,0,sizeof s->palette_valid);
    /* Reinitialize GPU storage too: clearing only the CPU shadow could make a
     * newly zero-valued job incorrectly look equal to stale nonzero GPU data. */
    for(unsigned i=0;i<5;++i){free(s->texture[i].shadow);s->texture[i].shadow=NULL;s->texture[i].width=s->texture[i].height=0;}
}
void genesis_hd_gpu_metrics(const GenesisHdGpu *s,GenesisHdGpuMetrics *out)
{if(out){if(s)*out=s->metrics;else memset(out,0,sizeof *out);}}
void genesis_hd_gpu_destroy(GenesisHdGpu *s)
{
    if(!s)return;
    if(SDL_GL_GetCurrentContext()==s->context){
        if(s->timers){for(unsigned i=0;i<HD_QUERY_SLOTS;++i)collect_query(s,i);s->gl.DeleteQueries(HD_QUERY_SLOTS*3,&s->query[0][0]);}
        GLuint names[5];for(unsigned i=0;i<5;++i)names[i]=s->texture[i].name;s->gl.DeleteTextures(5,names);s->gl.DeleteVertexArrays(1,&s->vao);s->gl.DeleteProgram(s->program);
    }
    const char *path=getenv("GENESIS_HD_TIMINGS");if(path&&*path&&s->samples){FILE *f=fopen(path,"wb");if(f){fprintf(f,"frame,scale,width,height,gpu_upload_ns,gpu_draw_ns,upload_bytes,cpu_prepare_ticks\n");for(unsigned i=0;i<s->sample_count;++i){HdSample *p=&s->samples[i];fprintf(f,"%llu,%u,%u,%u,%llu,%llu,%llu,%llu\n",(unsigned long long)p->serial,p->scale,p->width,p->height,(unsigned long long)p->upload_ns,(unsigned long long)p->draw_ns,(unsigned long long)p->upload_bytes,(unsigned long long)p->cpu_ticks);}fclose(f);}}
    fprintf(stderr,"[HD_GPU] frames=%llu upload_bytes=%llu source_bytes=%llu job_bytes=%llu gpu_samples=%llu gpu_upload_ns=%llu gpu_draw_ns=%llu cpu_ticks=%llu target=%ux%u bytes=%llu\n",(unsigned long long)s->metrics.frames,(unsigned long long)s->metrics.uploaded_bytes,(unsigned long long)s->metrics.source_upload_bytes,(unsigned long long)s->metrics.job_upload_bytes,(unsigned long long)s->metrics.gpu_samples,(unsigned long long)s->metrics.gpu_upload_ns,(unsigned long long)s->metrics.gpu_draw_ns,(unsigned long long)s->metrics.cpu_prepare_ticks,s->width,s->height,(unsigned long long)s->metrics.target_bytes);
    if(s->output)SDL_DestroyTexture(s->output);
    s->last_valid=0;
    for(unsigned i=0;i<5;++i)free(s->texture[i].shadow);
    free(s->samples);free(s);
}
int genesis_hd_gpu_readback(GenesisHdGpu *s,void *pixels,size_t pitch)
{
    if(!s||!s->output||!pixels||pitch<(size_t)s->width*4||pitch>INT_MAX)return 0;
    SDL_Texture *old=SDL_GetRenderTarget(s->renderer);if(SDL_SetRenderTarget(s->renderer,s->output))return 0;
    int good=!SDL_RenderReadPixels(s->renderer,NULL,SDL_PIXELFORMAT_ARGB8888,pixels,(int)pitch);if(SDL_SetRenderTarget(s->renderer,old))good=0;return good;
}

static int upload_sources(GenesisHdGpu *s,const GVDPRasterFrame *f,const ColorLut *lut)
{
    if(!texture_storage(s,2,GVDP_HD_PATTERNS,1,HD_TEX_WIDTH)||
       !texture_storage(s,3,GVDP_HD_PALETTES*GVDP_OUTPUT_PALETTE_SIZE,1,GVDP_OUTPUT_PALETTE_SIZE))return 0;
    uint8_t dirty_patterns[GVDP_HD_PATTERNS/HD_TEX_WIDTH]={0},dirty_palettes[GVDP_HD_PALETTES]={0};
    for(unsigned i=0;i<f->pattern_count;++i){unsigned slot=f->used_patterns[i];if(slot>=GVDP_HD_PATTERNS)return 0;
        uint32_t value=f->pattern_values[slot];if(!s->pattern_valid[slot]||s->texture[2].shadow[slot]!=value){s->texture[2].shadow[slot]=value;s->pattern_valid[slot]=1;dirty_patterns[slot/HD_TEX_WIDTH]=1;}}
    for(unsigned i=0;i<f->palette_count;++i){unsigned slot=f->used_palettes[i];if(slot>=GVDP_HD_PALETTES)return 0;
        const uint32_t *colors=f->palette_values[slot];uint32_t mapped[GVDP_OUTPUT_PALETTE_SIZE];
        if(lut&&!color_lut_is_passthrough(lut)){for(unsigned c=0;c<GVDP_OUTPUT_PALETTE_SIZE;++c)mapped[c]=color_lut_map_argb(lut,colors[c]);colors=mapped;}
        uint32_t *old=s->texture[3].shadow+(size_t)slot*GVDP_OUTPUT_PALETTE_SIZE;
        if(!s->palette_valid[slot]||memcmp(old,colors,sizeof mapped)){memcpy(old,colors,sizeof mapped);s->palette_valid[slot]=1;dirty_palettes[slot]=1;}}
    for(unsigned unit=2;unit<=3;++unit){unsigned count=unit==2?GVDP_HD_PATTERNS/HD_TEX_WIDTH:GVDP_HD_PALETTES;
        const uint8_t *dirty=unit==2?dirty_patterns:dirty_palettes;unsigned first=UINT_MAX;
        for(unsigned row=0;row<count;++row){if(dirty[row]){if(first==UINT_MAX)first=row;}else if(first!=UINT_MAX){upload_range(s,unit,first,row-first);first=UINT_MAX;}}
        if(first!=UINT_MAX)upload_range(s,unit,first,count-first);}
    return 1;
}
static int valid_frame(const GVDPRasterFrame *f)
{
    if(!f||!f->valid||!f->width||f->width>GVDP_MAX_WIDTH||!f->height||f->height>GVDP_HD_ROWS||
       f->row_count!=f->height||f->span_count>GVDP_HD_SPANS||f->pattern_count>GVDP_HD_PATTERNS||f->palette_count>GVDP_HD_PALETTES)return 0;
    for(unsigned y=0;y<f->height;++y){const GVDPHdRow *r=&f->rows[y];
        if(r->width!=f->width||r->start>r->width||r->count>r->width-r->start||r->palette>=GVDP_HD_PALETTES||r->backdrop>=256||r->bar>=256||
           r->a_first>f->span_count||r->a_count>f->span_count-r->a_first||r->b_first>f->span_count||r->b_count>f->span_count-r->b_first||
           r->s_first>f->span_count||r->s_count>f->span_count-r->s_first||r->ops_first>GVDP_HD_OP_WORDS-(f->width+7)/8)return 0;}
    for(unsigned i=0;i<f->span_count;++i){const GVDPHdSpan *p=&f->spans[i];unsigned n=p->run&255u,first=(p->run>>8)&255u;
        if(!n||n>8||first+n>8||p->source>=GVDP_HD_PATTERNS||p->x>=f->width||n>f->width-p->x||(p->color&255u)>240)return 0;}
    return 1;
}
static int ensure_target(GenesisHdGpu *s,const GVDPRasterFrame *f,unsigned scale)
{
    unsigned requested_w=f->width*scale,requested_h=f->height*scale;
    if(s->output&&s->requested_w==requested_w&&s->requested_h==requested_h)return 1;
    unsigned w=requested_w,h=requested_h;
    while(w>s->max_texture||h>s->max_texture||(uint64_t)w*h*4>HD_MAX_TARGET_BYTES){w=(w+1)/2;h=(h+1)/2;}
    SDL_Texture *next=NULL;
    while(!next){next=SDL_CreateTexture(s->renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,(int)w,(int)h);
        if(next)break;
        if(w==1&&h==1)return 0;
        w=(w+1)/2;h=(h+1)/2;}
    SDL_SetTextureBlendMode(next,SDL_BLENDMODE_NONE);
#if SDL_VERSION_ATLEAST(2,0,12)
    SDL_SetTextureScaleMode(next,SDL_ScaleModeNearest);
#endif
    if(s->output)SDL_DestroyTexture(s->output);
    s->last_valid=0;
    s->output=next;s->width=w;s->height=h;s->requested_w=requested_w;s->requested_h=requested_h;
    s->metrics.requested_scale=scale;s->metrics.output_width=w;s->metrics.output_height=h;s->metrics.target_bytes=(uint64_t)w*h*4;
    fprintf(stderr,"[HD_GPU] target requested=%ux%u allocated=%ux%u scale=%u bytes=%llu admitted=%s\n",requested_w,requested_h,w,h,scale,(unsigned long long)s->metrics.target_bytes,w==requested_w&&h==requested_h?"full":"reduced");return 1;
}
/* Deliberate, opt-in qualification readback. It is absent from normal frame
 * work and timing runs. CPU comparison is native jobs plus sampled GPU output. */
static void verify_output(GenesisHdGpu *s,const GVDPRasterFrame *f,const ColorLut *lut)
{
    const char *at=getenv("GENESIS_HD_VERIFY_AT");if(!at||!*at)return;
    char *end=NULL;unsigned long long n=strtoull(at,&end,10);
    if(!end||*end||n!=s->metrics.frames)return;
    uint32_t *native=(uint32_t*)malloc((size_t)f->width*f->height*4);
    uint32_t *gpu=(uint32_t*)malloc((size_t)s->width*s->height*4);
    if(!native||!gpu||!gvdp_hd_render_argb(f,native,f->width)||!genesis_hd_gpu_readback(s,gpu,(size_t)s->width*4)){
        fprintf(stderr,"[HD_VERIFY] failed to obtain qualification readback\n");free(native);free(gpu);return;
    }
    uint64_t mismatch=0;unsigned firstx=0,firsty=0;uint32_t expected_first=0,actual_first=0;
    for(unsigned y=0;y<s->height;++y)for(unsigned x=0;x<s->width;++x){
        uint32_t expected=native[(size_t)(y*f->height/s->height)*f->width+x*f->width/s->width];
        if(lut&&!color_lut_is_passthrough(lut))expected=color_lut_map_argb(lut,expected);
        uint32_t actual=gpu[(size_t)y*s->width+x];
        if(actual!=expected){if(!mismatch){firstx=x;firsty=y;expected_first=expected;actual_first=actual;}++mismatch;}
    }
    fprintf(stderr,"[HD_VERIFY] frame=%llu serial=%llu output=%ux%u pixels=%llu mismatches=%llu first=%u,%u expected=%08x actual=%08x\n",
        (unsigned long long)s->metrics.frames,(unsigned long long)f->serial,s->width,s->height,
        (unsigned long long)s->width*s->height,(unsigned long long)mismatch,firstx,firsty,expected_first,actual_first);
    const char *path=getenv("GENESIS_HD_CAPTURE");if(path&&*path)png_write_argb(path,gpu,(int)s->width,(int)s->height,(int)s->width);
    free(native);free(gpu);
}
SDL_Texture *genesis_hd_gpu_render(GenesisHdGpu *s,const GVDPRasterFrame *f,unsigned scale,const ColorLut *lut)
{
    if(!s||s->failed||scale<1||scale>64||!valid_frame(f))return NULL;
    if(SDL_GL_GetCurrentContext()!=s->context){s->failed=1;fprintf(stderr,"[HD_GPU] context changed; native fallback\n");return NULL;}
    uint64_t start=SDL_GetPerformanceCounter(),before=s->metrics.uploaded_bytes;
    if(!ensure_target(s,f,scale))return NULL;
    int graded=lut&&!color_lut_is_passthrough(lut);
    if(s->last_valid&&s->last_serial==f->serial&&s->last_graded==graded&&
       (!graded||!memcmp(&s->last_lut,lut,sizeof *lut)))return s->output;
    SDL_Texture *old=SDL_GetRenderTarget(s->renderer);if(SDL_SetRenderTarget(s->renderer,s->output))return NULL;
    if(SDL_RenderFlush(s->renderer)){SDL_SetRenderTarget(s->renderer,old);return NULL;}
    unsigned slot=s->query_cursor++%HD_QUERY_SLOTS;int timed=0;
    if(s->timers){collect_query(s,slot);timed=!s->pending[slot];}
    HdState state;state_begin(&s->gl,&state);
    if(timed)s->gl.QueryCounter(s->query[slot][0],GL_TIMESTAMP);
    int ok=upload_jobs(s,0,(const uint32_t*)f->rows,f->height*20,4)&&
        upload_jobs(s,1,(const uint32_t*)f->spans,f->span_count*4,4)&&
        upload_jobs(s,4,f->operator_words,f->height*(GVDP_MAX_WIDTH/8),1)&&upload_sources(s,f,lut);
    if(ok){
        static const char *names[]={"rows_tex","spans_tex","patterns_tex","palettes_tex","operators_tex"};
        s->gl.UseProgram(s->program);s->gl.BindVertexArray(s->vao);
        for(unsigned i=0;i<5;++i){bind_texture(s,i);s->gl.Uniform1i(s->gl.GetUniformLocation(s->program,names[i]),(GLint)i);}
        s->gl.Uniform2i(s->gl.GetUniformLocation(s->program,"native_size"),(GLint)f->width,(GLint)f->height);
        s->gl.Uniform2i(s->gl.GetUniformLocation(s->program,"output_size"),(GLint)s->width,(GLint)s->height);
        s->gl.Viewport(0,0,(GLsizei)s->width,(GLsizei)s->height);
        if(timed)s->gl.QueryCounter(s->query[slot][1],GL_TIMESTAMP);
        s->gl.DrawArrays(GL_TRIANGLES,0,3);
        if(timed)s->gl.QueryCounter(s->query[slot][2],GL_TIMESTAMP);
        GLenum error=s->gl.GetError();if(error!=GL_NO_ERROR){fprintf(stderr,"[HD_GPU] draw error=0x%x; native fallback\n",(unsigned)error);ok=0;}
    }
    state_end(&s->gl,&state);if(SDL_SetRenderTarget(s->renderer,old))ok=0;
    uint64_t ticks=SDL_GetPerformanceCounter()-start;s->metrics.cpu_prepare_ticks+=ticks;
    if(ok){s->last_valid=1;s->last_serial=f->serial;s->last_graded=graded;if(graded)s->last_lut=*lut;
        ++s->metrics.frames;if(timed){s->pending[slot]=1;s->query_serial[slot]=f->serial;s->query_bytes[slot]=s->metrics.uploaded_bytes-before;s->query_cpu[slot]=ticks;s->query_scale[slot]=scale;s->query_width[slot]=s->width;s->query_height[slot]=s->height;}verify_output(s,f,lut);return s->output;}
    s->failed=1;return NULL;
}

#else
struct GenesisHdGpu {int unused;};
GenesisHdGpu *genesis_hd_gpu_create(SDL_Renderer *r,SDL_Texture *t){(void)r;(void)t;return NULL;}
void genesis_hd_gpu_destroy(GenesisHdGpu *s){(void)s;}
void genesis_hd_gpu_invalidate(GenesisHdGpu *s){(void)s;}
void genesis_hd_gpu_metrics(const GenesisHdGpu *s,GenesisHdGpuMetrics *m){(void)s;if(m)memset(m,0,sizeof *m);}
int genesis_hd_gpu_readback(GenesisHdGpu *s,void *p,size_t n){(void)s;(void)p;(void)n;return 0;}
SDL_Texture *genesis_hd_gpu_render(GenesisHdGpu *s,const GVDPRasterFrame *f,unsigned scale,const ColorLut *c){(void)s;(void)f;(void)scale;(void)c;return NULL;}
#endif
