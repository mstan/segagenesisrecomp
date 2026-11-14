/* Real chip/filter/drain contract, without a device or game. Shadow stubs
 * observe its canonical input and model an already-proven separate output. */
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
extern "C" {
#include "audio/ym2612.h"
#include "audio/sn76489.h"
#include "audio/mixer.h"
#include "audio/event_queue.h"
#include "audio/fm_shadow.h"
unsigned long g_snd_frame;
uint64_t ym2612_cosim_hash(void);
uint64_t psg_cosim_hash(void);
int audio_event_pop(AudioEvent *) { return 0; }
void audio_event_requeue(const AudioEvent *,size_t) {}
}
#define CHECK(c) do { if(!(c)) { std::fprintf(stderr,"audio filter line%d: %s\n",__LINE__,#c);std::exit(1); } } while(0)
static bool shadow_active;
static bool worker_enabled;
static unsigned worker_jobs;
static uint64_t shadow_input;
extern "C" void fm_shadow_init(void) {}
extern "C" void fm_shadow_advance(uint32_t) {}
extern "C" void fm_shadow_write(uint8_t,uint8_t) {}
extern "C" int fm_shadow_active(void) { return shadow_active; }
extern "C" int fm_shadow_enabled(void) { return worker_enabled; }
static void execute(AudioMixerJob first,void *a,AudioMixerJob second,void *b,void *)
{ first(a);second(b);worker_jobs+=2; }
extern "C" void fm_shadow_verify_and_substitute(int16_t *out,size_t n)
{
    shadow_input=14695981039346656037ull;
    for(size_t i=0;i<n*2;++i)shadow_input=(shadow_input^(uint16_t)out[i])*1099511628211ull;
    if(shadow_active)std::fill(out,out+n*2,2222);
}
using Bytes=std::vector<uint8_t>;
struct State { Bytes fm,psg; };
static State save()
{
    State s;s.fm.resize(ym2612_rb_save(nullptr,0));s.psg.resize(psg_rb_save(nullptr,0));
    CHECK(ym2612_rb_save(s.fm.data(),s.fm.size())==s.fm.size());
    CHECK(psg_rb_save(s.psg.data(),s.psg.size())==s.psg.size());return s;
}
static void load(const State &s)
{ CHECK(ym2612_rb_load(s.fm.data(),s.fm.size()));CHECK(psg_rb_load(s.psg.data(),s.psg.size())); }
static bool equal(const State &a,const State &b) { return a.fm==b.fm && a.psg==b.psg; }
static void reg(uint8_t a,uint8_t d) { ym2612_write(0,a);ym2612_write(1,d); }
static void setup(int bypass)
{
    shadow_active=false;audio_mixer_set_filter_bypass(bypass);audio_mixer_init();
    audio_mixer_set_shadow_executor(worker_enabled?execute:nullptr,nullptr);
    ym2612_write(2,0xb6);ym2612_write(3,0xc0);reg(0x2b,0x80);reg(0x2a,0xd0);
    psg_write(0x87);psg_write(0);psg_write(0x90);psg_write(0xe4);psg_write(0xf4);
}
struct Pair { std::vector<int16_t> fm,raw_fm,psg,raw_psg; };
static Pair reference(uint32_t cycles)
{
    ym2612_advance(cycles);psg_advance(cycles);psg_reset_leftover();Pair p;
    size_t fn=ym2612_samples_available(),pn=psg_samples_available();
    p.fm.resize(fn*2);p.raw_fm.resize(fn*2);p.psg.resize(pn);p.raw_psg.resize(pn);
    CHECK(ym2612_render_pair(p.fm.data(),p.raw_fm.data(),fn)==fn);
    CHECK(psg_render_pair(p.psg.data(),p.raw_psg.data(),pn)==pn);return p;
}
static Pair drain(uint32_t cycles)
{
    Pair p;p.fm.resize(2048);p.psg.resize(8192);size_t fn=0,pn=0;
    audio_mixer_drain(cycles,p.fm.data(),1024,&fn,p.psg.data(),8192,&pn);
    p.fm.resize(fn*2);p.psg.resize(pn);return p;
}
static void check_fade(const std::vector<int16_t> &actual,const std::vector<int16_t> &filtered,
                       const std::vector<int16_t> &raw,unsigned channels,unsigned span,bool to_raw)
{
    CHECK(actual.size()==filtered.size() && actual.size()==raw.size());
    for(size_t i=0;i<actual.size()/channels;++i) {
        unsigned mix=(unsigned)std::min(i,(size_t)span);if(!to_raw)mix=span-mix;
        for(unsigned c=0;c<channels;++c) {
            size_t k=i*channels+c;
            int16_t expected=(int16_t)(((int64_t)filtered[k]*(span-mix)+(int64_t)raw[k]*mix)/span);
            CHECK(actual[k]==expected);
        }
    }
}
static void test_selection_and_fade()
{
    setup(0);State before=save();Pair expected=reference(1008*8);State after=save();load(before);
    Pair normal=drain(1008*8);CHECK(normal.fm==expected.fm && normal.psg==expected.psg);
    CHECK(equal(save(),after));
    for(int bypass:{1,0}) {
        before=save();expected=reference(1008*512);after=save();load(before);
        CHECK(expected.fm!=expected.raw_fm && expected.psg!=expected.raw_psg);
        audio_mixer_set_filter_bypass(bypass);Pair actual=drain(1008*512);
        check_fade(actual.fm,expected.fm,expected.raw_fm,2,ym2612_sample_rate()/200,bypass!=0);
        check_fade(actual.psg,expected.psg,expected.raw_psg,1,psg_sample_rate()/200,bypass!=0);
        CHECK(equal(save(),after));
    }
    /* Startup/reinit chooses the preference immediately, with no stale fade. */
    setup(1);before=save();expected=reference(1008*512);after=save();load(before);
    Pair raw=drain(1008*512);CHECK(raw.fm==expected.raw_fm && raw.psg==expected.raw_psg);
    CHECK(equal(save(),after));uint64_t canon_input=shadow_input;
    setup(0);load(before);normal=drain(1008*512);CHECK(equal(save(),after));CHECK(canon_input==shadow_input);
    CHECK(normal.fm==expected.fm && normal.psg==expected.psg);
    setup(-7);load(before);raw=drain(1008*512);CHECK(raw.fm==expected.raw_fm && raw.psg==expected.raw_psg);
    /* A separately proven enhanced FM output remains authoritative. */
    setup(1);load(before);shadow_active=true;raw=drain(1008*512);
    CHECK(std::all_of(raw.fm.begin(),raw.fm.end(),[](int16_t v){return v==2222;}));
    CHECK(raw.psg==expected.raw_psg && canon_input==shadow_input);
}
static void test_filter_history_and_restore()
{
    setup(0);ym2612_advance(1008*128);psg_advance(240*512);State pending=save();
    CHECK(ym2612_samples_available()==128 && psg_samples_available()==512);
    CHECK(!ym2612_render_pair(nullptr,nullptr,1) && !psg_render_pair(nullptr,nullptr,1));
    Pair expected=reference(0);load(pending);audio_mixer_set_filter_bypass(1);
    CHECK(equal(save(),pending));Pair replay=reference(0);
    CHECK(replay.fm==expected.fm && replay.raw_fm==expected.raw_fm);
    CHECK(replay.psg==expected.psg && replay.raw_psg==expected.raw_psg);
    /* Independently check the original LPF response against its raw input.
     * This DAC/tone fixture is below clipping, so its gain-matched raw member
     * is exactly the original filter input. */
    int prev[2]={0,0},out[2]={0,0};
    for(size_t i=0;i<expected.fm.size();++i) {
        unsigned c=(unsigned)i&1;int in=expected.raw_fm[i];
        int next=(int)(((int64_t)(in+prev[c])*9484+(int64_t)out[c]*46568)/65536);
        CHECK(expected.fm[i]==next);prev[c]=in;out[c]=next;
    }
    int last=0,value=0;
    for(size_t i=0;i<expected.psg.size();++i) {
        int in=expected.raw_psg[i];int next=(int)(((int64_t)(in+last)*2517+(int64_t)value*60504)/65536);
        CHECK(expected.psg[i]==next);last=in;value=next;
    }
    /* File snapshots discard pending PCM by contract, retaining both filter
     * histories and chip clocks. New samples after load must match exactly. */
    FILE *fm=std::tmpfile(),*psg=std::tmpfile();CHECK(fm && psg);
    CHECK(ym2612_save_state(fm) && psg_save_state(psg));
    Pair next=reference(1008*128);std::rewind(fm);std::rewind(psg);
    CHECK(ym2612_load_state(fm) && psg_load_state(psg));
    CHECK(!ym2612_samples_available() && !psg_samples_available());
    replay=reference(1008*128);
    CHECK(replay.fm==next.fm && replay.raw_fm==next.raw_fm);
    CHECK(replay.psg==next.psg && replay.raw_psg==next.raw_psg);
    std::fclose(fm);std::fclose(psg);
}

static uint64_t full_chip_hash()
{ return ym2612_cosim_hash() ^ (psg_cosim_hash()*1099511628211ull); }
static Pair peek()
{
    Pair p;size_t f=ym2612_samples_available(),n=psg_samples_available();
    p.fm.resize(f*2);p.raw_fm.resize(f*2);p.psg.resize(n);p.raw_psg.resize(n);
    CHECK(ym2612_peek_audition(p.fm.data(),p.raw_fm.data(),f)==f);
    CHECK(psg_peek_audition(p.psg.data(),p.raw_psg.data(),n)==n);return p;
}
static bool ac(const std::vector<int16_t> &p)
{ return !p.empty() && *std::min_element(p.begin(),p.end())!=*std::max_element(p.begin(),p.end()); }
static void configure_fm()
{
    reg(0x22,0x0f); /* shared LFO must still clock with any output mask */
    reg(0x2b,0);
    for(unsigned channel=0;channel<6;++channel) {
        unsigned part=channel<3?0:2,ch=channel%3;
        auto wr=[part](unsigned a,unsigned d){ym2612_write((uint8_t)part,(uint8_t)a);ym2612_write((uint8_t)(part+1),(uint8_t)d);};
        wr(0xb0+ch,7);wr(0xb4+ch,0xc0);
        for(unsigned op=0;op<4;++op) {
            wr(0x30+op*4+ch,1);wr(0x40+op*4+ch,0x30);wr(0x50+op*4+ch,0x1f);
            wr(0x60+op*4+ch,8);wr(0x80+op*4+ch,0x0f);
        }
        wr(0xa4+ch,0x22);wr(0xa0+ch,0x40+ch*11);reg(0x28,0xf0|ch|(part?4:0));
    }
}
static void test_audition()
{
    setup(0);audio_mixer_set_audition(0,0x7ff);configure_fm();
    psg_write(0xc7);psg_write(3);psg_write(0xd2);psg_write(0xe7);psg_write(0xf4);
    State initial=save();Pair full=reference(1008*512);uint64_t hash=full_chip_hash();
    for(unsigned mask=0;mask<12;++mask) {
        load(initial);unsigned selected=mask?1u<<(mask-1):0;
        audio_mixer_set_audition(1,selected);
        ym2612_advance(1008*512);psg_advance(1008*512);psg_reset_leftover();
        Pair audition=peek(),canonical=reference(0);
        CHECK(canonical.fm==full.fm && canonical.raw_fm==full.raw_fm && canonical.psg==full.psg && canonical.raw_psg==full.raw_psg);
        CHECK(full_chip_hash()==hash); /* includes feedback, envelopes, LFO, tone3/noise counters */
        if(mask>=1 && mask<=6)CHECK(ac(audition.raw_fm));
        if(mask==0 || mask==7)CHECK(!ac(audition.raw_fm)); /* no DAC enabled */
        if(mask==11)CHECK(ac(audition.raw_psg)); /* noise still driven by inaudible tone3 */
        if(!mask)CHECK(!ac(audition.raw_psg));
    }
    /* Keep a varying DAC audible while excluding the mutually exclusive FM6. */
    setup(0);audio_mixer_set_audition(1,64);Pair dac;
    for(unsigned i=0;i<24;++i){reg(0x2a,i&1?0x50:0xd0);ym2612_advance(1008*16);}
    dac=peek();CHECK(ac(dac.raw_fm));
    setup(0);audio_mixer_set_audition(1,32);
    for(unsigned i=0;i<24;++i){reg(0x2a,i&1?0x50:0xd0);ym2612_advance(1008*16);}
    CHECK(!ac(peek().raw_fm));
    /* Pending PCM and independent audition filter histories survive rollback. */
    setup(0);configure_fm();audio_mixer_set_audition(1,1u<<5);
    ym2612_advance(1008*90);psg_advance(240*500);State pending=save();Pair expected=peek();
    reference(1008*20);load(pending);Pair replay=peek();
    CHECK(replay.fm==expected.fm && replay.raw_fm==expected.raw_fm && replay.psg==expected.psg);
    CHECK(equal(save(),pending));
    State invalid=pending;uint32_t chip_size=0,bad_mask=128;
    std::memcpy(&chip_size,invalid.fm.data(),4);std::memcpy(invalid.fm.data()+4+chip_size+20,&bad_mask,4);
    CHECK(!ym2612_rb_load(invalid.fm.data(),invalid.fm.size()) && equal(save(),pending));
    bad_mask=16;std::memcpy(invalid.psg.data()+invalid.psg.size()-500*8-16,&bad_mask,4);
    CHECK(!psg_rb_load(invalid.psg.data(),invalid.psg.size()) && equal(save(),pending));
    FILE *fm=std::tmpfile(),*psg=std::tmpfile();CHECK(fm&&psg);
    CHECK(ym2612_save_state(fm)&&psg_save_state(psg));reference(0);
    ym2612_advance(1008*300);psg_advance(1008*300);expected=peek();reference(0);
    std::rewind(fm);std::rewind(psg);CHECK(ym2612_load_state(fm)&&psg_load_state(psg));
    ym2612_advance(1008*300);psg_advance(1008*300);replay=peek();
    CHECK(replay.fm==expected.fm && replay.raw_fm==expected.raw_fm && replay.psg==expected.psg);
    std::fclose(fm);std::fclose(psg);
    /* Both mixer paths give the verifier full canonical PCM before muting. */
    for(int bypass:{0,1}) {
        setup(bypass);audio_mixer_set_audition(0,0x7ff);configure_fm();initial=save();
        Pair normal=drain(1008*512);uint64_t oracle=shadow_input;hash=full_chip_hash();
        setup(bypass);load(initial);audio_mixer_set_audition(1,1u<<5);shadow_active=true;
        ym2612_advance(1008*512);psg_advance(1008*512);psg_reset_leftover();expected=peek();
        load(initial);audio_mixer_set_audition(1,1u<<5);Pair actual=drain(1008*512);
        CHECK(oracle==shadow_input && full_chip_hash()==hash);
        CHECK(actual.fm==(bypass?expected.raw_fm:expected.fm));
        CHECK(actual.psg==(bypass?expected.raw_psg:expected.psg));
        CHECK(actual.fm!=normal.fm);
    }
    audio_mixer_set_audition(0,0x7ff);
}
int main()
{
    test_selection_and_fade();test_filter_history_and_restore();test_audition();
    worker_enabled=true;test_selection_and_fade();test_filter_history_and_restore();test_audition();
    CHECK(worker_jobs>0);
    std::puts("Hardware filter: default PCM, raw pairing, equal clocks/state,5ms live fades, output-only channel audition, shadow oracle and current-build restore PASS");
}
