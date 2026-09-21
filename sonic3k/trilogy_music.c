/* SMPS1/2 -> SMPS S&K. Command and voice translations follow the pinned
 * disassemblies' _smps2asm_inc.asm. All music/sample bytes come from ROM. */
#include "trilogy_music.h"
#include "trilogy_assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const uint8_t tr_music_ids[TR_MUSIC_CUES]={2,0x19,0x29,0x2A,0x2C,0x27,0x31};
/* None of these envelope IDs is referenced by an S3K sound effect. */
const uint8_t tr_music_envelope_ids[13]={1,2,4,5,6,7,8,9,10,11,12,16,18};
static unsigned le(const uint8_t *p){return p[0]+256u*p[1];}
static unsigned be(const uint8_t *p){return 256u*p[0]+p[1];}
static unsigned be32(const uint8_t *p){return be(p)*65536u+be(p+2);}
static void put(uint8_t *p,unsigned n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static unsigned tempo(unsigned pack,unsigned n)
{
    if(pack==1){if(!n)n=256;n=((((n-1)<<8)+(n>>1))/n)&255;}
    return (256-(n?n:1))&255;
}
static size_t saxman_data(const uint8_t *p,size_t end,uint8_t *out,size_t cap)
{
    size_t pos=0,n=0;
    memset(out,0,cap);
    while(pos<end){unsigned flags=p[pos++];
        for(unsigned bit=0;bit<8&&pos<end;++bit){
            if(flags&(1u<<bit)){if(n==cap)return 0;out[n++]=p[pos++];}
            else{if(end-pos<2)return 0;unsigned lo=p[pos++],hi=p[pos++];
                int from=(int)((((lo|((hi&240)<<4))+0x12)&0xFFF)+(n&~0xFFFu));unsigned count=(hi&15)+3;
                if(from>(int)n)from-=0x1000;
                if(count>cap-n)return 0;
                /* A reference before the first dictionary window fills zeroes. */
                if(from<0){memset(out+n,0,count);n+=count;}
                else while(count--){out[n++]=out[from++];}
            }
        }
    }
    return n;
}
typedef struct Fix {unsigned at,pc,kind;} Fix;
typedef struct Convert {
    const uint8_t *src;unsigned size,base,pack,n,maxvoice;
    uint8_t *out;uint16_t map[3][0x8000];
    Fix fixes[8192];unsigned fix_count;
    unsigned work_pc[8192],work_kind[8192],work_count;
    int bad;
} Convert;
static void emit(Convert *c,unsigned n)
{if(c->n>=0x8000)c->bad=1;else c->out[c->n++]=(uint8_t)n;}
static unsigned get(Convert *c,unsigned *pc)
{if(*pc>=c->size){c->bad=1;return 0;}return c->src[(*pc)++];}
static unsigned pointer(Convert *c,unsigned *pc,int header)
{
    unsigned at=*pc,lo=get(c,pc),hi=get(c,pc),n;
    if(c->pack==1)n=header?lo*256+hi:(unsigned)((int)at+1+(int16_t)(lo*256+hi));
    else n=(lo+hi*256)-c->base;
    if(n>=c->size)c->bad=1;return n;
}
static void target(Convert *c,unsigned pc,unsigned kind)
{
    if(c->fix_count>=8192||c->work_count>=8192){c->bad=1;return;}
    c->fixes[c->fix_count++]=(Fix){c->n,pc,kind};emit(c,0);emit(c,0);
    c->work_pc[c->work_count]=pc;c->work_kind[c->work_count++]=kind;
}
static unsigned env(Convert *c,unsigned value)
{if(!value)return 0;if(value> (c->pack==1?9u:13u)){c->bad=1;return 0;}return tr_music_envelope_ids[value-1];}
static int convert_song(unsigned pack,const uint8_t *src,unsigned size,unsigned base,uint8_t *out,unsigned *bytes)
{
    Convert *c=calloc(1,sizeof *c);if(!c)return 0;
    c->src=src;c->size=size;c->base=base;c->pack=pack;c->out=out;
    memset(c->map,255,sizeof c->map);
    unsigned pc=0,voices=pointer(c,&pc,1),fm=get(c,&pc),psg=get(c,&pc),div=get(c,&pc),speed=get(c,&pc);
    if(!fm||fm>7||psg>3||!div)c->bad=1;
    emit(c,0);emit(c,0);emit(c,fm);emit(c,psg);emit(c,div);emit(c,tempo(pack,speed));
    for(unsigned i=0;i<fm+psg&&!c->bad;++i){
        unsigned kind=i>=fm?2:(i?1:0),ptr=pointer(c,&pc,1);target(c,ptr,kind);
        unsigned pitch=get(c,&pc),vol=get(c,&pc);emit(c,pitch+(kind==2?12:0));emit(c,vol);
        if(kind==2){get(c,&pc);unsigned e=get(c,&pc);emit(c,0);emit(c,env(c,e));}
    }
    for(unsigned work=0;work<c->work_count&&!c->bad;++work){
        pc=c->work_pc[work];unsigned kind=c->work_kind[work];
        if(pc<size&&c->map[kind][pc]!=0xFFFF)continue;
        while(!c->bad){
            if(pc>=size){c->bad=1;break;}
            if(c->map[kind][pc]!=0xFFFF){emit(c,0xF6);target(c,pc,kind);break;}
            c->map[kind][pc]=(uint16_t)c->n;
            unsigned command=get(c,&pc),a,b,d,e;
            if(command<0xE0){emit(c,command);continue;}
            switch(command){
            case 0xE0:case 0xE1:case 0xE2:case 0xE6:case 0xE8:case 0xEC:case 0xF3:
                emit(c,command==0xE6&&kind==2?0xEC:command);emit(c,get(c,&pc));break;
            case 0xE3:emit(c,0xF9);goto done;
            case 0xE4:emit(c,0xE2);emit(c,0xFF);emit(c,0xF2);goto done;
            /* Not used by these scores. S&K has no per-channel divider
             * command (FF08 belongs to a different, modified driver). */
            case 0xE5:c->bad=1;break;
            case 0xE7:emit(c,command);break;
            case 0xE9:emit(c,0xFB);emit(c,get(c,&pc));break;
            case 0xEA:emit(c,0xFF);emit(c,0);emit(c,tempo(pack,get(c,&pc)));break;
            case 0xEB:emit(c,0xFF);emit(c,4);emit(c,get(c,&pc));break;
            case 0xEE:case 0xF2:emit(c,0xF2);goto done;
            case 0xEF:a=get(c,&pc);if(a>=128){c->bad=1;break;}if(a>c->maxvoice)c->maxvoice=a;emit(c,command);emit(c,a);break;
            case 0xF0:
                a=get(c,&pc);b=get(c,&pc);d=get(c,&pc);e=get(c,&pc);
                emit(c,command);emit(c,a+1);emit(c,b);emit(c,d);emit(c,(e+1)*b);break;
            case 0xF1:emit(c,0xF4);emit(c,0x80);break;
            case 0xF4:emit(c,0xFA);break;
            case 0xF5:emit(c,command);emit(c,env(c,get(c,&pc)));break;
            case 0xF6:case 0xF8:
                a=pointer(c,&pc,0);emit(c,command);target(c,a,kind);if(command==0xF6)goto done;break;
            case 0xF7:
                a=get(c,&pc);b=get(c,&pc);d=pointer(c,&pc,0);
                emit(c,command);emit(c,a);emit(c,b);target(c,d,kind);break;
            default:c->bad=1;break;
            }
        }
done:;
    }
    if(c->n+(c->maxvoice+1)*25>0x8000||voices+(c->maxvoice+1)*25>size)c->bad=1;
    if(!c->bad){
        put(out,c->n+0x8000);
        for(unsigned v=0;v<=c->maxvoice;++v){
            const uint8_t *s=src+voices+v*25;unsigned at=c->n;emit(c,s[0]);
            for(unsigned group=0;group<6;++group)for(unsigned op=0;op<4;++op)
                emit(c,s[1+group*4+(pack==2&&op==1?2:pack==2&&op==2?1:op)]);
            unsigned algorithm=s[0]&7;
            for(unsigned op=0;op<4;++op)out[at+21+op]&=127;
            out[at+24]|=128;if(algorithm>=5)out[at+23]|=128;
            if(algorithm>=4)out[at+22]|=128;if(algorithm==7)out[at+21]|=128;
        }
        for(unsigned i=0;i<c->fix_count;++i){Fix f=c->fixes[i];
            if(f.pc>=size||c->map[f.kind][f.pc]==0xFFFF){c->bad=1;break;}
            put(out+f.at,0x8000+c->map[f.kind][f.pc]);
        }
    }
    *bytes=c->n;int ok=!c->bad;free(c);return ok;
}
int tr_music_decode(unsigned pack,const uint8_t *rom,size_t size,TrMusic *out,char *error,size_t error_size)
{
    if((pack!=1&&pack!=2)||size!=(pack==1?0x80000u:0x100000u))return 0;
    memset(out,0,sizeof *out);
    /* Stage, boss, act clear, 1-up, invincibility, game over, drowning. */
    static const unsigned s1_ids[]={0,11,13,7,6,14,17};
    static const unsigned s2_at[]={0xF88C4,0xFB81E,0xFD35E,0xFD48D,0xF8359,0xFD57A,0xF823B};
    uint8_t buffer[0x8000],driver[0x2000];
    size_t driver_bytes=pack==1?tr_kosinski(rom+0x72E7C,size-0x72E7C,driver,sizeof driver):
        saxman_data(rom+0xEC0E8,be(rom+0xEC050)-1,driver,sizeof driver);
    if(!driver_bytes){snprintf(error,error_size,"Sonic %u sound driver decode failed",pack);return 0;}
    for(unsigned cue=0;cue<TR_MUSIC_CUES;++cue){
        unsigned at=pack==1?be32(rom+0x71A9C+s1_ids[cue]*4):s2_at[cue];
        if(at>=size)goto invalid;
        unsigned n=(unsigned)(size-at),base=pack==1?0:0x8000+(at&0x7FFF);const uint8_t *p=rom+at;
        if(pack==2&&cue!=3&&cue!=5){unsigned compressed=le(p);if(compressed>n-2)goto invalid;
            n=(unsigned)saxman_data(p+2,compressed,buffer,sizeof buffer);p=buffer;base=0x1380;}
        if(n>0x8000)n=0x8000;
        if(!n||!convert_song(pack,p,n,base,out->song[cue],out->song_bytes+cue)){
            snprintf(error,error_size,"Sonic %u music cue %02X conversion failed",pack,tr_music_ids[cue]);return 0;}
    }
    out->envelopes=pack==1?9:13;
    for(unsigned i=0;i<out->envelopes;++i){
        unsigned at=pack==1?be32(rom+0x719A8+i*4):le(driver+0x1029+i*2);
        const uint8_t *p=pack==1?rom:driver;size_t cap=pack==1?size:driver_bytes;
        unsigned j=0;for(;j<128&&at+j<cap;++j){unsigned b=p[at+j];out->envelope[i][j]=(uint8_t)(b==0x80?0x81:b);if(b==0x80)break;}
        if(j==128||at+j>=cap)goto invalid;
    }
    unsigned data=0x200,locations[7]={0};out->drum_count=pack==1?11:17;
    for(unsigned i=0;i<out->drum_count;++i){
        unsigned sample=0,rate,at,length;
        if(pack==1){sample=i==0?0:i==1?1:2;const uint8_t *meta=driver+0xD6+sample*8;
            at=le(meta);length=le(meta+2);rate=meta[4];
            if(i>=7)rate=rom[0x71CC4+i-7];
        }else{const uint8_t *meta=driver+0x124F+i*2;sample=meta[0]-0x81;rate=meta[1];
            if(sample>=7)goto invalid;at=0xE8000+(le(driver+0x1233+sample*4)&0x7FFF);length=le(driver+0x1235+sample*4);
        }
        const uint8_t *p=pack==1?driver:rom;size_t cap=pack==1?driver_bytes:size;
        if(at>cap||length>cap-at||data+length>sizeof out->drums)goto invalid;
        if(!locations[sample]){locations[sample]=data;memcpy(out->drums+data,p+at,length);data+=length;}
        unsigned header=0x80+i*5;put(out->drums+i*2,0x8000+header);out->drums[header]=(uint8_t)rate;
        put(out->drums+header+1,length);put(out->drums+header+3,0x8000+locations[sample]);
    }
    return 1;
invalid:snprintf(error,error_size,"Sonic %u sound data out of bounds",pack);return 0;
}
