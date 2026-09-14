/* Host-only Blue Spheres presentation, included by sonic3_video.c.
 * The native 32x32 board, movement, collisions and collection remain the
 * authority. No guest memory, perspective table, SAT or ROM is patched.
 *
 * A perspective ellipsoid replaces the 40-column prerecorded globe. Its
 * horizontal radius follows the canvas, while local tile and sprite scale
 * stay fixed. This exposes more board instead of stretching 320 pixels.
 * The camera is fitted to the stock cardinal sphere tables (about 1px RMS).
 * All projection/intersection work is cached across frames and resizes.
 */
#include <math.h>

enum { SS_HEIGHT=224, SS_TEXTURE=64, SS_KINDS=14 };
#ifdef SONIC3_STANDALONE
enum { SS_EXTRA=0x8400 };
#else
enum { SS_EXTRA=0xE480 };
#endif
static const double ss_radius=15.18430653,ss_distance=3.35173470;
static const double ss_focal=345.3564589,ss_pitch=.69874597;
static const double ss_lift=.42983566,ss_ground_lift=.64;
typedef struct { float x,z,depth; } SSPoint;
typedef struct { int x,y,w,h; float depth; unsigned kind,lod; } SSSphere;
static SSPoint *ss_ground;
static SSSphere *ss_spheres;
static unsigned ss_count,ss_capacity,ss_margin_count;
static int ss_width,ss_ready;
static double ss_stretch,ss_sin,ss_cos,ss_x,ss_y;
static uint8_t ss_board[1024],ss_texture[SS_KINDS][16][SS_TEXTURE*SS_TEXTURE];
static uint16_t ss_texture_ready[SS_KINDS];
static unsigned ss_color_phase,ss_clear;
static uint8_t ss_state[65536];
static int ss_latched;

static void ss_capture(void)
{
    unsigned mode=g_ram[0xF600]&127;
    ss_latched=mode==0x34 || mode==0x48;
    if(!ss_latched)return;
    /* Latch at IRQ entry, before the native SAT/palette DMA. Reading live
     * RAM at visible line zero can instead see the next partially executed
     * movement/collection tick alongside the previous character sprites. */
    memcpy(ss_state+0xE420,g_ram+0xE420,0x32);
    memcpy(ss_state+SS_EXTRA,g_ram+SS_EXTRA,SS_KINDS*8);
    memcpy(ss_state+0xF100,g_ram+0xF100,1024);
}

static unsigned ss_word(const uint8_t *p,unsigned a)
{ return ((unsigned)p[a]<<8)|p[a+1]; }
static unsigned ss_long(const uint8_t *p,unsigned a)
{ return (ss_word(p,a)<<16)|ss_word(p,a+2); }
static unsigned ss_rom_word(unsigned a)
{ return a<0x3FFFFFu?ss_word(g_rom,a):0; }

/* Inverse of the perspective projection, intersecting the near hemisphere.
 * x/stretch, z, h-radius lie on a sphere. Returning the near root provides
 * a depth buffer too: spheres on the far side do not shine through it. */
static SSPoint ss_intersect(double px,double py,double stretch)
{
    double p=px/ss_focal,q=(py-112)/ss_focal,b=ss_pitch;
    double az=(1-b*q)/(1+b*b),bz=(-ss_distance+b*ss_ground_lift)/(1+b*b);
    double ah=(q+b)/(1+b*b),bh=(-ss_ground_lift-b*ss_distance)/(1+b*b)-ss_radius;
    double aa=p*p/(stretch*stretch)+az*az+ah*ah;
    double bb=2*(az*bz+ah*bh),cc=bz*bz+bh*bh-ss_radius*ss_radius;
    double disc=bb*bb-4*aa*cc;
    SSPoint result={0,0,0};
    if(disc<0)return result;
    double t=(-bb-sqrt(disc))/(2*aa);
    if(t<=0)return result;
    result.x=(float)(p*t);result.z=(float)(az*t+bz);result.depth=(float)t;
    return result;
}
static int ss_project(double x,double z,double stretch,double *px,double *py,double *depth)
{
    double rad=ss_radius*ss_radius-x*x/(stretch*stretch)-z*z;
    if(rad<=0)return 0;
    double h=ss_radius-sqrt(rad),d=ss_distance+z+ss_pitch*h;
    if(d<=.4)return 0;
    *px=ss_focal*x/d;*py=112+ss_focal*(ss_lift-ss_pitch*z+h)/d;*depth=d;
    return 1;
}
static int ss_depth_order(const void *a,const void *b)
{
    float x=((const SSSphere *)a)->depth,y=((const SSSphere *)b)->depth;
    return x<y?1:x>y?-1:0;
}
static void ss_load_texture(const GVDP *v,unsigned kind,unsigned lod)
{
    unsigned entry=SS_EXTRA+kind*8,map=ss_long(ss_state,entry);
    unsigned anim=ss_word(ss_state,entry+6);
    unsigned frame=(anim&0x7FFF)+((anim&0x8000)?0:lod);
    unsigned gfx=ss_word(ss_state,entry+4);
    memset(ss_texture[kind][lod],0,sizeof ss_texture[kind][lod]);
    ss_texture_ready[kind]|=(uint16_t)(1u<<lod);
    if(map>=0x400000u || frame>255)return;
    unsigned p=map+(int16_t)ss_rom_word(map+frame*2),count=ss_rom_word(p);p+=2;
    if(count>16)return;
    for(unsigned n=0;n<count && p+6<=0x400000u;++n,p+=6) {
        int y=(int8_t)g_rom[p]+32,x=(int16_t)ss_rom_word(p+4)+32;
        int cw=((g_rom[p+1]>>2)&3)+1,ch=(g_rom[p+1]&3)+1;
        unsigned attr=(ss_rom_word(p+2)+gfx)&65535u;
        for(int dy=0;dy<ch*8;++dy)for(int dx=0;dx<cw*8;++dx) {
            int tx=x+dx,ty=y+dy;
            if(tx<0 || ty<0 || tx>=SS_TEXTURE || ty>=SS_TEXTURE)continue;
            int ix=(attr&0x800)?cw*8-1-dx:dx,iy=(attr&0x1000)?ch*8-1-dy:dy;
            unsigned tile=(attr&0x7FF)+(ix/8)*ch+iy/8;
            unsigned a=(tile*32+(iy&7)*4+(ix&7)/2)&65535u;
            unsigned pixel=(v->vram[a]>>((ix&1)?0:4))&15;
            if(pixel)ss_texture[kind][lod][ty*SS_TEXTURE+tx]=(uint8_t)(((attr>>9)&48)|pixel);
        }
    }
}
static void ss_begin(const GVDP *v,int width)
{
    ss_ready=0;ss_count=ss_margin_count=0;
    if(!ss_latched || width<320 || width>32768 || ss_long(ss_state,SS_EXTRA+16)>=0x400000u ||
       !ss_long(ss_state,SS_EXTRA+16))return;
    if(width!=ss_width) {
        SSPoint *points=(SSPoint *)realloc(ss_ground,(size_t)width*SS_HEIGHT*sizeof *points);
        if(!points)return;
        ss_ground=points;ss_width=width;ss_stretch=width/320.0;
        for(int y=0;y<SS_HEIGHT;++y)for(int x=0;x<width;++x)
            ss_ground[(size_t)y*width+x]=ss_intersect(x-(width-1)*.5,y,ss_stretch);
    }
    memcpy(ss_board,ss_state+0xF100,sizeof ss_board);
    ss_x=ss_word(ss_state,0xE422)/256.0;ss_y=ss_word(ss_state,0xE424)/256.0;
    double angle=ss_state[0xE426]*(6.2831853071795864769/256);
    ss_sin=sin(angle);ss_cos=cos(angle);
    ss_color_phase=(ss_state[0xE42F]&14)/2;ss_clear=ss_word(ss_state,0xE44A);
    memset(ss_texture_ready,0,sizeof ss_texture_ready);
    /* Bounding box of a rotated visible strip, not the original 15x16
     * scan. Board coordinates wrap; host sprite storage grows on demand. */
    double extent=8*ss_stretch+2;
    int rx=(int)ceil(fabs(ss_cos)*extent+fabs(ss_sin)*11)+1;
    int ry=(int)ceil(fabs(ss_sin)*extent+fabs(ss_cos)*11)+1;
    int cx=(int)floor(ss_x),cy=(int)floor(ss_y);
    for(int iy=cy-ry;iy<=cy+ry;++iy)for(int ix=cx-rx;ix<=cx+rx;++ix) {
        unsigned kind=ss_board[((unsigned)iy&31)*32+((unsigned)ix&31)];
        if(!kind || kind>=SS_KINDS)continue;
        double dx=ix-ss_x,dy=iy-ss_y;
        double x=dx*ss_cos-dy*ss_sin,z=-dx*ss_sin-dy*ss_cos;
        if(z< -2 || z>10 || fabs(x)>extent)continue;
        double px,py,depth;
        if(!ss_project(x,z,ss_stretch,&px,&py,&depth))continue;
        if(ss_clear){px*=1+ss_clear/256.0;py-=ss_clear;}
        /* Reuse the artist-authored distance frames, including far spheres
         * and animated rings. Only enlarge objects nearer than native LOD0. */
        int lod=(int)(depth*2)-6;
        if(lod<0)lod=0;
        if(lod>15)lod=15;
        int size=depth<ss_distance?(int)(64*ss_distance/depth):64;
        if(size>256)size=256;
        int sx=(int)floor(px+(width-1)*.5-size*.5+.5),sy=(int)floor(py-size*.5+.5);
        if(sx>=width || sx+size<=0 || sy>=SS_HEIGHT || sy+size<=0)continue;
        if(ss_count==ss_capacity) {
            unsigned capacity=ss_capacity?ss_capacity*2:512;
            SSSphere *sprites=(SSSphere *)realloc(ss_spheres,capacity*sizeof *sprites);
            if(!sprites)return;
            ss_spheres=sprites;ss_capacity=capacity;
        }
        if(!(ss_texture_ready[kind]&(1u<<lod)))ss_load_texture(v,kind,(unsigned)lod);
        ss_spheres[ss_count++]=(SSSphere){sx,sy,size,size,(float)depth,kind,(unsigned)lod};
        if(sx<(width-320)/2 || sx+size>(width+320)/2)++ss_margin_count;
    }
    qsort(ss_spheres,ss_count,sizeof *ss_spheres,ss_depth_order);
    ss_ready=1;
}
static void ss_draw_spheres(int line,uint32_t *out,int width,const uint32_t *palette,int near)
{
    for(unsigned n=0;n<ss_count;++n) {
        const SSSphere *s=&ss_spheres[n];
        if((s->depth<ss_distance)!=near || line<s->y || line>=s->y+s->h)continue;
        int ty=(line-s->y)*SS_TEXTURE/s->h;
        for(int x=s->x<0?0:s->x;x<s->x+s->w && x<width;++x) {
            unsigned p=ss_texture[s->kind][s->lod][ty*SS_TEXTURE+(x-s->x)*SS_TEXTURE/s->w];
            if(!p)continue;
            float ground=ss_ground[(size_t)line*width+x].depth;
            if(!ss_clear && s->depth>8 && ground && s->depth>ground+.15f)continue;
            out[x]=palette[p];
        }
    }
}
static void ss_native_sprites(const GVDP *v,int line,uint32_t *out,int width,
                              const uint32_t *palette,const uint32_t *shadow,int text)
{
    unsigned base=(v->reg[5]&127u)<<9,index=0,entries[80],count=0;
    do {
        unsigned a=(base+index*8)&65535u;
        entries[count++]=a;index=v->vram[(a+3)&65535]&127;
    } while(index && count<80);
    /* Characters, arrows and messages retain their original mapping and
     * pixel scale. Spheres/rings are replaced, never drawn twice. */
    /* SAT's first sprite wins: paint back-to-front, especially operators
     * behind Sonic and Tails rather than darkening the characters. */
    for(unsigned n=count;n-->0;) {
        unsigned a=entries[n],attr=vram16(v,a+4),tile=attr&0x7FF;
        int is_text=tile<0x5A7 || (tile>=0x6F8 && tile<0x6FC);
        if((is_text || tile>=0x77B) && is_text==text) {
            int sy=(vram16(v,a)&511)-128,sx=(vram16(v,a+6)&511)-128;
            if(tile>=0x589 && tile<0x59B && sy<32)
                sx+=sx<160?0:width-320;
            else sx+=(width-320)/2;
            int cw=((v->vram[(a+2)&65535]>>2)&3)+1,ch=(v->vram[(a+2)&65535]&3)+1;
            if(line>=sy && line<sy+ch*8) {
                int iy=line-sy;if(attr&0x1000)iy=ch*8-1-iy;
                for(int dx=0;dx<cw*8;++dx) {
                    int x=sx+dx;if(x<0 || x>=width)continue;
                    int ix=(attr&0x800)?cw*8-1-dx:dx;
                    unsigned t=(tile+(ix/8)*ch+iy/8)&0x7FF;
                    unsigned address=(t*32+(iy&7)*4+(ix&7)/2)&65535;
                    unsigned p=(v->vram[address]>>((ix&1)?0:4))&15;
                    if(!p)continue;
                    p|=(attr>>9)&48;
                    if(p==63 && (v->reg[12]&8)) {
                        /* Shadow operator: halve the currently drawn color.
                         * Use the DAC table for matching palette entries. */
                        for(unsigned k=0;k<64;++k)if(out[x]==palette[k]){out[x]=shadow[k];break;}
                    } else if(p!=62 || !(v->reg[12]&8))out[x]=palette[p];
                }
            }
        }
    }
}
static void ss_scanline(const GVDP *v,int line,uint32_t *out,int width,
                         const uint32_t *palette,const uint32_t *shadow,int hs_b)
{
    unsigned base_b=(v->reg[4]&7u)<<13,base_a=(v->reg[2]&56u)<<10;
    unsigned color0=56+(ss_color_phase&7),color1=56+((ss_color_phase+4)&7);
    int by=line+(v->vsram[1]&1023),origin=(width-320)/2;
    for(int x=0;x<width;++x) {
        const SSPoint *p=&ss_ground[(size_t)line*width+x];
        if(p->depth) {
            double wx=ss_x+p->x*ss_cos-p->z*ss_sin;
            double wy=ss_y-p->x*ss_sin-p->z*ss_cos;
            int parity=((int)floor(wx+.5)+(int)floor(wy+.5))&1;
            out[x]=palette[parity?color1:color0];
        } else {
            int bx=x-origin-hs_b;
            unsigned attr=plane_attr(v,base_b,bx,by),pixel=pattern_pixel(v,(uint16_t)attr,bx,by);
            unsigned color=pixel?pixel:v->reg[7]&63;
            out[x]=(attr&0x8000)?palette[color]:shadow[color];
        }
    }
    ss_draw_spheres(line,out,width,palette,0);
    ss_native_sprites(v,line,out,width,palette,shadow,0);
    ss_draw_spheres(line,out,width,palette,1);
    ss_native_sprites(v,line,out,width,palette,shadow,1);
    /* Counters are plane-A tiles, not SAT sprites. Move their two complete
     * 64x24 panels to the actual left/right edges; no centered HUD ghost. */
    if(line>=8 && line<32)for(int side=0;side<2;++side)for(int dx=0;dx<64;++dx) {
        int source=(side?240:16)+dx,x=(side?width-80:16)+dx;
        unsigned attr=plane_attr(v,base_a,source,line);
        unsigned pixel=pattern_pixel(v,(uint16_t)attr,source,line);
        if(pixel && x>=0 && x<width)out[x]=palette[pixel];
    }
}
