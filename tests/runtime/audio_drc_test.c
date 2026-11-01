/* Portable DRC stall-history/period tests plus an informational callback-cost
 * benchmark. Timings are reported, never used as flaky CI pass thresholds. */
#define RECOMP_AUDIO_DRC_IMPL
#include "recomp_audio_drc.h"
#include <assert.h>
#include <stdio.h>
#include <time.h>

/* A 250-ms producer pause consumes the initial cushion. Resume at normal
 * cadence (no burst of catch-up frames) and require one bounded recovery,
 * rather than starving again on every new video-frame-sized chunk. */
static void test_stall_recovery(double rate, int host_frames)
{
    rab_config cfg; rab_config_defaults(&cfg); cfg.source_rate=rate;
    rab_bridge b; assert(!rab_init(&b,&cfg));
    int seed=(int)(rate*.1), chunk=(int)(rate/60);
    int16_t *in=(int16_t *)calloc((size_t)seed*2,sizeof *in); assert(in);
    for(int i=0;i<seed;++i) in[2*i]=in[2*i+1]=(int16_t)(10000*sin(2*RAB_PI*220*i/rate));
    rab_push(&b,in,seed);
    double next=1000.0/60, next_pull=host_frames/48.0;
    int16_t out[1024*2];
    for(int ms=1;ms<=2000;++ms) {
        if(ms>=next) {
            next+=1000.0/60;
            if(ms<500 || ms>=750) rab_push(&b,in,chunk);
        }
        if(ms>=next_pull) {
            next_pull+=host_frames/48.0;
            rab_pull(&b,out,host_frames);
        }
    }
    printf("recovery rate=%.0f block=%d episodes=%llu repeated=%llu fill_ms=%.2f\n",rate,host_frames,
           (unsigned long long)b.stats.stretch_events,
           (unsigned long long)b.stats.stretch_frames,rab_fill_ms(&b));
    fflush(stdout);
    assert(b.stats.stretch_events==1);
    assert(!b.concealing && rab_fill_ms(&b)>cfg.target_ms/4);
    assert(!b.stats.underrun_events && !b.stats.overflow_drops);
    free(in); rab_free(&b);
}

static void test_scaled_publication(void)
{
    rab_config cfg;rab_config_defaults(&cfg);cfg.source_rate=223721;
    rab_bridge a,b;assert(!rab_init(&a,&cfg));assert(!rab_init(&b,&cfg));
    const int gains[]={256,128,0,1,255,85,192};
    int16_t original[4096],scaled[4096],out_a[2048],out_b[2048];
    for(unsigned block=0;block<180;++block) {
        int count=block%13u==0?2048:731;
        int gain=gains[block%7u];
        for(int i=0;i<count*2;++i) {
            int16_t sample=(int16_t)((unsigned)(i*983u+block*8191u)&65535u);
            original[i]=(int16_t)((int32_t)sample*gain/256);scaled[i]=sample;
        }
        /* rab_push and rab_pull are unchanged original production code. */
        rab_push(&a,original,count);rab_push_scaled(&b,scaled,count,gain);
        assert(!memcmp(original,scaled,(size_t)count*2*sizeof *scaled));
        assert(!memcmp(a.ring,b.ring,(size_t)a.cap*2*sizeof *a.ring));
        rab_bridge sa=a,sb=b;sa.ring=sb.ring=NULL;sa.coeffs=sb.coeffs=NULL;
        assert(!memcmp(&sa,&sb,sizeof sa));
        if(block%3u==0) {
            rab_pull(&a,out_a,1024);rab_pull(&b,out_b,1024);
            assert(!memcmp(out_a,out_b,sizeof out_a));
        }
        if(block==100){rab_reset(&a);rab_reset(&b);}
    }
    /* Explicit scalar overflow fallback and huge-count floating-point edge. */
    for(unsigned edge=0;edge<2;++edge) {
        a.in_count=b.in_count=edge?UINT64_C(9007199254740993):100000u;
        a.out_pos=b.out_pos=(double)a.in_count-(double)a.cap;
        for(unsigned i=0;i<256;++i){scaled[i]=(int16_t)(i*127u);original[i]=(int16_t)((int32_t)scaled[i]*85/256);}
        rab_push(&a,original,128);rab_push_scaled(&b,scaled,128,85);
        assert(!memcmp(a.ring,b.ring,(size_t)a.cap*2*sizeof *a.ring));
        rab_bridge sa=a,sb=b;sa.ring=sb.ring=NULL;sa.coeffs=sb.coeffs=NULL;
        assert(!memcmp(&sa,&sb,sizeof sa));
    }
    rab_free(&a);rab_free(&b);
}

int main(void)
{
    test_scaled_publication();
    const double rates[]={22050,44100,48000,96000,223721};
    for(unsigned r=0;r<sizeof rates/sizeof rates[0];++r) {
        rab_config cfg; rab_config_defaults(&cfg); cfg.source_rate=rates[r];
        rab_bridge b; assert(!rab_init(&b,&cfg));
        int frames=(int)(rates[r]*.1);
        int16_t *in=(int16_t *)calloc((size_t)frames*2,sizeof *in); assert(in);
        for(int i=0;i<frames;++i) in[2*i]=in[2*i+1]=(int16_t)(12000*sin(2*RAB_PI*440*i/rates[r]));
        rab_push(&b,in,frames);
        int64_t end=(int64_t)b.in_count-b.half-1;
        double lag=rab__find_loop_len(&b,end);
        assert(lag>=floor(rates[r]*.005) && lag<=floor(rates[r]*.025));
        double cycles=lag*440/rates[r];
        assert(fabs(cycles-floor(cycles+.5))<.025);
        clock_t before=clock(); volatile double used=0;
        for(unsigned i=0;i<4;++i) used+=rab__find_loop_len(&b,end);
        printf("rate=%.0f lag=%.0f search_ms=%.3f checksum=%.0f\n",rates[r],lag,
               1000.0*(clock()-before)/CLOCKS_PER_SEC/4,used);
        int16_t out[1024*2];
        for(unsigned i=0;i<12;++i) rab_pull(&b,out,1024);
        assert(b.stats.stretch_events && b.stats.stretch_frames && !b.stats.underrun_events);
        rab_reset(&b); assert(!b.primed && !b.stats.stretch_events);
        memset(in,0,(size_t)frames*2*sizeof *in); rab_push(&b,in,frames);
        lag=rab__find_loop_len(&b,end); assert(lag==floor(rates[r]*.005));
        free(in); rab_free(&b);
        test_stall_recovery(rates[r],480);
        test_stall_recovery(rates[r],1024);
    }
    puts("PASS DRC loop bounds, tone continuity, silence, concealment and reset at five sample rates");
    return 0;
}
