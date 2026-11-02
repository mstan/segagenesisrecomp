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
int main()
{
    test_selection_and_fade();test_filter_history_and_restore();
    worker_enabled=true;test_selection_and_fade();test_filter_history_and_restore();
    CHECK(worker_jobs>0);
    std::puts("Hardware filter: default PCM, raw pairing, equal clocks/state,5ms live fades, shadow oracle and current-build restore PASS");
}
