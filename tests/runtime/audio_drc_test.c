/* Portable DRC stall-history/period tests plus an informational callback-cost
 * benchmark. Timings are reported, never used as flaky CI pass thresholds. */
#define RECOMP_AUDIO_DRC_IMPL
#include "recomp_audio_drc.h"
#include <assert.h>
#include <stdio.h>
#include <time.h>

static void assert_same_bridge(const rab_bridge *a, const rab_bridge *b)
{
    rab_bridge copy = *b;
    copy.ring = a->ring;
    copy.coeffs = a->coeffs;
    assert(!memcmp(a, &copy, sizeof copy));
    assert(!memcmp(a->ring, b->ring,
                   (size_t)a->cap * a->cfg.channels * sizeof(float)));
}

/* The original S16 push remains the independent scalar oracle. Compare its
 * complete ring/controller/concealment state, not just pushed-frame totals. */
static void test_prepared_publication(void)
{
    const int gains[] = {0, 1, 93, 128, 255, 256};
    for (int ch = 1; ch <= 2; ++ch) {
        rab_config cfg; rab_config_defaults(&cfg);
        cfg.channels = ch; cfg.source_rate = 223721;
        rab_bridge a, b; assert(!rab_init(&a,&cfg)); assert(!rab_init(&b,&cfg));
        int max_frames = (int)a.cap + 3;
        int16_t *s16 = (int16_t *)malloc((size_t)max_frames * ch * sizeof *s16);
        float *prepared = (float *)malloc((size_t)max_frames * ch * sizeof *prepared);
        assert(s16 && prepared);
        for (unsigned g=0; g<sizeof gains/sizeof gains[0]; ++g) {
            for (int mode=0; mode<9; ++mode) {
                rab_reset(&a); rab_reset(&b);
                int frames = mode == 5 ? max_frames : 37;
                uint64_t start = mode == 1 ? (uint64_t)a.cap*3-11 :
                                 mode == 4 ? UINT64_C(9007199254740992)+18 : 111;
                if(mode==7 || mode==8)start=UINT64_C(9007199254740992)-37+(mode==8);
                a.in_count=b.in_count=start;
                a.out_pos=b.out_pos=(double)start-(mode==2 ? (double)a.cap-2 : 100.25);
                if (mode==3) a.out_pos=b.out_pos=(double)start-(double)a.cap+37;
                if (mode==6) a.out_pos=b.out_pos=NAN;
                for (int i=0; i<frames*ch; ++i) {
                    int16_t original=(int16_t)(i*7919+g*47);
                    s16[i]=(int16_t)((int32_t)original*gains[g]/256);
                    prepared[i]=(float)s16[i]*(1.0f/32768.0f);
                }
                int pushed = rab_try_push_prepared(&b,prepared,frames);
                if (!pushed) {
                    assert_same_bridge(&a,&b); /* refusal is transactional */
                    rab_push(&b,s16,frames);
                }
                if (mode==1 || mode==7) assert(pushed); /* wrap/exact bound */
                if (mode==2 || mode==3 || mode==4 || mode==5 || mode==6 || mode==8) assert(!pushed);
                rab_push(&a,s16,frames);
                assert_same_bridge(&a,&b);
                int16_t oa[1024*2], ob[1024*2];
                for (int p=0; mode!=6 && p<3; ++p) {
                    rab_pull(&a,oa,1024); rab_pull(&b,ob,1024);
                    assert(!memcmp(oa,ob,(size_t)1024*ch*sizeof *oa));
                    assert_same_bridge(&a,&b);
                }
            }
        }
        /* Steady production, a producer stall/conceal episode, recovery and
         * reset all use the actual prepared producer against the scalar one. */
        rab_reset(&a); rab_reset(&b);
        for (int tick=0; tick<130; ++tick) {
            if (tick<40 || tick>=65) {
                int frames=3728;
                for (int i=0; i<frames*ch; ++i) {
                    s16[i]=(int16_t)(i*53+tick*101);
                    prepared[i]=(float)s16[i]*(1.0f/32768.0f);
                }
                if (!rab_try_push_prepared(&b,prepared,frames)) rab_push(&b,s16,frames);
                rab_push(&a,s16,frames);
            }
            int16_t oa[800*2],ob[800*2];
            rab_pull(&a,oa,800); rab_pull(&b,ob,800);
            assert(!memcmp(oa,ob,(size_t)800*ch*sizeof *oa));
            assert_same_bridge(&a,&b);
            if (tick==110) {rab_reset(&a);rab_reset(&b);assert_same_bridge(&a,&b);}
        }
        free(s16); free(prepared); rab_free(&a); rab_free(&b);
    }
    puts("PASS prepared publication: gain, wrap, boundary, overflow, large counts, pull, conceal, recovery, reset");
}

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

int main(void)
{
    test_prepared_publication();
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
