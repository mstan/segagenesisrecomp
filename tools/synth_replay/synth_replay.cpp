/*
 * synth_replay.cpp — offline "ours vs theirs" synth parity harness.
 *
 * Reads a chip_ring.txt dump (the exact FM port/value + PSG byte stream the
 * SMPS driver produced, captured by the always-on [CHIP-TRACE] ring) and
 * replays that IDENTICAL stream — with identical per-event timing derived from
 * the (frame, scanline) stamps — through BOTH synths:
 *
 *   OURS   = runner/audio/ym2612_ymfm.cpp (ymfm) + runner/audio/sn76489.c
 *   THEIRS = clownmdemu-core fm.c + psg.c + low-pass-filter.c (the reference
 *            that sounded correct before — AGPL, DEV-ONLY oracle, never shipped)
 *
 * Both are run through the SAME mixer (audio.c's FM/1 + PSG/8, nearest-neighbour
 * FM upsample to PSG rate), so the only difference in the output is the synth
 * core + its low-pass filtering. Emits ours.wav, theirs.wav, diff.wav
 * (ours - theirs, the audible delta) and a per-window divergence report.
 *
 * This is throwaway measurement instrumentation (PRINCIPLES #18) — it links the
 * AGPL clownmdemu synth purely to OBSERVE it, exactly like the _oracle build.
 */
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>

/* ---- our synth (stable C API) ---- */
extern "C" {
    void   ym2612_init(void);
    void   ym2612_advance(uint32_t cycles_master);
    void   ym2612_write(uint8_t port, uint8_t value);
    size_t ym2612_render(int16_t *out, size_t sample_count);
    size_t ym2612_samples_available(void);
    void   psg_init(void);
    void   psg_advance(uint32_t cycles_master);
    void   psg_write(uint8_t value);
    size_t psg_render(int16_t *out, size_t sample_count);
    size_t psg_samples_available(void);
}

/* ---- clownmdemu synth ---- */
extern "C" {
    #include "fm.h"
    #include "psg.h"
    #include "low-pass-filter.h"
}

/* Scheduler timing constants (must match genesis_machine.c). */
static const uint64_t MASTER_PER_LINE  = 3420;
static const uint64_t MASTER_PER_FRAME = 3420ull * 262ull; /* 896040 */
static const uint32_t FM_MASTER_PER_SAMPLE  = 1008; /* 144 * 7  -> 53267 Hz */
static const uint32_t PSG_MASTER_PER_SAMPLE = 240;  /* 15 * 16  -> 223721 Hz */
static const int FM_VOL_DIV  = 1;
static const int PSG_VOL_DIV  = 8;

struct Event { uint64_t mc; uint8_t kind; uint8_t port; uint8_t val; }; /* kind 0=FM 1=PSG */

/* Raw ring line before mixer-order reconstruction. */
struct RawEvent {
    uint32_t wf;     /* wall frame — the mixer's drain unit */
    uint32_t mc;     /* within-frame cycle stamp the mixer sorts by */
    uint32_t idx;    /* capture order — stable-sort tiebreak */
    uint8_t  kind;   /* 0=FM 1=PSG */
    uint8_t  port;
    uint8_t  val;
};

/* Reorder capture-order events into mixer apply order and assign absolute
 * master-cycle times. Mirrors runner/audio/mixer.c (the synth's authority;
 * same algorithm as tools/chip_ring_decode.py — capture order is NOT stamp
 * order because the own backend pushes events from two stamp axes):
 *   1. Group per wall frame (wf), the mixer's drain unit.
 *   2. FM pair guard: each FM ADDRESS write (p0/p2) inherits the stamp of its
 *      matching DATA write so no foreign event sorts between the pair.
 *   3. Stable sort by (mc, capture idx) within the frame.
 *   4. Absolute time = wf * MASTER_PER_FRAME + mc, clamped monotonic across
 *      the ordered stream (the mixer clamps per chip; a single non-decreasing
 *      timeline yields the identical per-chip advance deltas). Events stamped
 *      past the frame end land at their real later-frame time, which is
 *      exactly where mixer.c's multi-frame deferral replays them. */
static std::vector<Event> mixer_order(std::vector<RawEvent> &raw)
{
    std::stable_sort(raw.begin(), raw.end(), [](const RawEvent &a, const RawEvent &b) {
        return a.wf < b.wf;
    });
    std::vector<Event> out;
    out.reserve(raw.size());
    uint64_t prev_abs = 0;
    for (size_t lo = 0; lo < raw.size(); ) {
        size_t hi = lo;
        while (hi < raw.size() && raw[hi].wf == raw[lo].wf) hi++;
        /* Pair guard within the frame. */
        std::vector<uint32_t> stamp(hi - lo);
        for (size_t i = lo; i < hi; i++) stamp[i - lo] = raw[i].mc;
        for (size_t i = lo; i < hi; i++) {
            if (raw[i].kind != 0 || (raw[i].port != 0 && raw[i].port != 2)) continue;
            for (size_t j = i + 1; j < hi; j++) {
                if (raw[j].kind != 0) continue;
                if (raw[j].port == raw[i].port + 1) { stamp[i - lo] = raw[j].mc; break; }
                if (raw[j].port == raw[i].port)     break; /* re-latched address */
            }
        }
        std::vector<size_t> order(hi - lo);
        for (size_t i = 0; i < order.size(); i++) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            if (stamp[a] != stamp[b]) return stamp[a] < stamp[b];
            return raw[lo + a].idx < raw[lo + b].idx;
        });
        for (size_t k : order) {
            const RawEvent &r = raw[lo + k];
            uint64_t abs_mc = (uint64_t)r.wf * MASTER_PER_FRAME + stamp[k];
            if (abs_mc < prev_abs) abs_mc = prev_abs;
            prev_abs = abs_mc;
            Event e; e.mc = abs_mc; e.kind = r.kind; e.port = r.port; e.val = r.val;
            out.push_back(e);
        }
        lo = hi;
    }
    return out;
}

static int16_t clamp16(int32_t v) { return v > 32767 ? 32767 : (v < -32768 ? -32768 : (int16_t)v); }

/* audio.c's mixer: iterate at PSG rate, upsample FM nearest-neighbour. */
static std::vector<int16_t> mix(const std::vector<int16_t> &fm /*stereo*/,
                                const std::vector<int16_t> &psg /*mono*/)
{
    size_t psg_frames = psg.size();
    size_t fm_frames  = fm.size() / 2;
    std::vector<int16_t> out(psg_frames * 2);
    for (size_t i = 0; i < psg_frames; i++) {
        int32_t p = (int32_t)psg[i] / PSG_VOL_DIV;
        int32_t l = p, r = p;
        if (fm_frames > 0) {
            size_t fi = (size_t)((unsigned long long)i * fm_frames / psg_frames);
            l += (int32_t)fm[fi * 2 + 0] / FM_VOL_DIV;
            r += (int32_t)fm[fi * 2 + 1] / FM_VOL_DIV;
        }
        out[i * 2 + 0] = clamp16(l);
        out[i * 2 + 1] = clamp16(r);
    }
    return out;
}

static void write_wav(const char *path, const std::vector<int16_t> &s, uint32_t rate)
{
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return; }
    uint32_t data_bytes = (uint32_t)(s.size() * sizeof(int16_t));
    uint32_t total = 36 + data_bytes; uint16_t ch = 2, bits = 16;
    uint32_t byte_rate = rate * ch * (bits / 8); uint16_t block = ch * (bits / 8);
    uint32_t fmt = 16; uint16_t pcm = 1;
    fwrite("RIFF",1,4,f); fwrite(&total,4,1,f); fwrite("WAVE",1,4,f);
    fwrite("fmt ",1,4,f); fwrite(&fmt,4,1,f); fwrite(&pcm,2,1,f); fwrite(&ch,2,1,f);
    fwrite(&rate,4,1,f); fwrite(&byte_rate,4,1,f); fwrite(&block,2,1,f); fwrite(&bits,2,1,f);
    fwrite("data",1,4,f); fwrite(&data_bytes,4,1,f);
    fwrite(s.data(),1,data_bytes,f); fclose(f);
}

/* ---- OURS ---- */
static void render_ours(const std::vector<Event> &ev,
                        std::vector<int16_t> &fm_all, std::vector<int16_t> &psg_all)
{
    ym2612_init(); psg_init();
    int16_t tmp[4096];
    uint64_t prev = ev.empty() ? 0 : ev[0].mc;
    /* Advance in small chunks, draining between, so a large inter-event gap
     * never overflows our fixed 8192-sample scratch (which drops when full) —
     * that would desync ours' sample count from theirs and drift the diff. */
    auto drain = [&]() {
        size_t n;
        while ((n = ym2612_samples_available()) > 0) {
            size_t got = ym2612_render(tmp, n < 2048 ? n : 2048);
            if (!got) break;
            for (size_t i = 0; i < got * 2; i++) fm_all.push_back(tmp[i]);
        }
        while ((n = psg_samples_available()) > 0) {
            size_t got = psg_render(tmp, n < 4096 ? n : 4096);
            if (!got) break;
            for (size_t i = 0; i < got; i++) psg_all.push_back(tmp[i]);
        }
    };
    for (const Event &e : ev) {
        uint32_t d = (uint32_t)(e.mc - prev); prev = e.mc;
        while (d) {
            uint32_t step = d > 2000u ? 2000u : d;
            ym2612_advance(step); psg_advance(step); d -= step;
            drain();
        }
        if (e.kind == 0) ym2612_write(e.port, e.val); else psg_write(e.val);
    }
}

/* ---- THEIRS (clownmdemu) ---- */
static void render_theirs(const std::vector<Event> &ev,
                          std::vector<int16_t> &fm_all, std::vector<int16_t> &psg_all)
{
    /* Zero the whole struct first: *_Initialise do NOT clear `configuration`
     * (fm_channels_disabled / dac_channel_disabled / tone_disabled / etc.).
     * The real core zero-inits all state; a stack struct would otherwise leave
     * channels "disabled" from garbage and render silence. */
    FM fm; PSG psg;
    memset(&fm, 0, sizeof fm); memset(&psg, 0, sizeof psg);
    FM_Initialise(&fm);
    PSG_Initialise(&psg);
    LowPassFilter_FirstOrder_State fm_lpf[2], psg_lpf[1];
    LowPassFilter_FirstOrder_Initialise(fm_lpf, 2);
    LowPassFilter_FirstOrder_Initialise(psg_lpf, 1);

    std::vector<int16_t> fbuf; fbuf.reserve(8192);
    uint64_t prev = ev.empty() ? 0 : ev[0].mc;
    uint64_t fm_acc = 0, psg_acc = 0;
    int psg_cmds = 0;
    for (const Event &e : ev) {
        uint32_t d = (uint32_t)(e.mc - prev); prev = e.mc;
        fm_acc += d; psg_acc += d;
        size_t fm_n = (size_t)(fm_acc / FM_MASTER_PER_SAMPLE);  fm_acc %= FM_MASTER_PER_SAMPLE;
        size_t psg_n = (size_t)(psg_acc / PSG_MASTER_PER_SAMPLE); psg_acc %= PSG_MASTER_PER_SAMPLE;
        if (fm_n) {
            fbuf.assign(fm_n * 2, 0);                       /* FM_OutputSamples accumulates */
            FM_OutputSamples(&fm, fbuf.data(), (cc_u32f)fm_n);
            LowPassFilter_FirstOrder_Apply(fm_lpf, 2, fbuf.data(), fm_n,
                LOW_PASS_FILTER_COMPUTE_MAGIC_FIRST_ORDER(6.910, 4.910));
            for (size_t i = 0; i < fm_n * 2; i++) fm_all.push_back(fbuf[i]);
        }
        if (psg_n) {
            fbuf.assign(psg_n, 0);                          /* PSG_Update accumulates */
            PSG_Update(&psg, fbuf.data(), psg_n);
            LowPassFilter_FirstOrder_Apply(psg_lpf, 1, fbuf.data(), psg_n,
                LOW_PASS_FILTER_COMPUTE_MAGIC_FIRST_ORDER(26.044, 24.044));
            for (size_t i = 0; i < psg_n; i++) psg_all.push_back(fbuf[i]);
        }
        if (e.kind == 0) {
            switch (e.port) {
                case 0: FM_DoAddress(&fm, 0, e.val); break;
                case 1: FM_DoData(&fm, e.val); break;
                case 2: FM_DoAddress(&fm, 1, e.val); break;
                case 3: FM_DoData(&fm, e.val); break;
            }
        } else {
            PSG_DoCommand(&psg, e.val);
            psg_cmds++;
        }
    }
    fprintf(stderr, "[theirs] PSG cmds=%d  final att: t0=%u t1=%u t2=%u noise=%u\n",
            psg_cmds, (unsigned)psg.state.tones[0].attenuation, (unsigned)psg.state.tones[1].attenuation,
            (unsigned)psg.state.tones[2].attenuation, (unsigned)psg.state.noise.attenuation);
}

static double rms(const std::vector<int16_t> &s, size_t lo, size_t hi, int stride, int off) {
    double acc = 0; size_t n = 0;
    for (size_t i = lo; i < hi; i += stride) { double v = s[i + off]; acc += v * v; n++; }
    return n ? std::sqrt(acc / n) : 0.0;
}

/* --fm-channel N: keep only FM events belonging to channel N (0..5) plus the
 * global registers (LFO $22, timers $24-$27, DAC $2A/$2B) and that channel's
 * key-on/off events on $28. PSG events are dropped. Stateful: an address write
 * selects the register; the following data write inherits its keep/drop fate.
 * Lets us attribute the ours-vs-theirs FM band deficit to a single channel,
 * since both cores replay the SAME filtered stream. */
static std::vector<Event> filter_fm_channel(const std::vector<Event> &ev, int ch)
{
    std::vector<Event> out;
    out.reserve(ev.size());
    int keep_data[2] = {1, 1};     /* per part: does the pending data write pass? */
    int addr_is_keyon[2] = {0, 0}; /* per part: pending data write targets $28 */
    for (const Event &e : ev) {
        if (e.kind != 0) continue;             /* drop PSG — FM attribution only */
        int part = (e.port >= 2) ? 1 : 0;
        if ((e.port & 1) == 0) {               /* address write */
            uint8_t a = e.val;
            int keep;
            addr_is_keyon[part] = 0;
            if (a < 0x30) {
                /* globals ($22 LFO, $24-$27 timers/ch3 mode, $2A/$2B DAC) — keep.
                 * $28 key-on: keep the address write; the DATA write decides. */
                keep = 1;
                if (part == 0 && a == 0x28) addr_is_keyon[part] = 1;
            } else {
                int cip = a & 3;               /* channel within part; 3 = invalid */
                keep = (cip != 3) && (part * 3 + cip == ch);
            }
            keep_data[part] = keep;
            if (keep) out.push_back(e);
        } else {                               /* data write */
            if (addr_is_keyon[part]) {
                /* $28 data: bits 0-2 select 0,1,2 (ch0-2) / 4,5,6 (ch3-5). */
                int sel = e.val & 7;
                int kch = (sel >= 4) ? (sel - 4 + 3) : sel;
                if (sel != 3 && sel != 7 && kch == ch) out.push_back(e);
            } else if (keep_data[part]) {
                out.push_back(e);
            }
        }
    }
    return out;
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "chip_ring.txt";
    int fm_channel = -1;
    for (int i = 2; i < argc; i++)
        if (!strcmp(argv[i], "--fm-channel") && i + 1 < argc)
            fm_channel = atoi(argv[++i]);
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return 1; }

    /* Self-test: drive clownmdemu PSG with a known loud ch0 tone + noise and
     * confirm it outputs — isolates "mis-driven" from "byte-stream" causes. */
    {
        PSG t; memset(&t, 0, sizeof t); PSG_Initialise(&t);
        PSG_DoCommand(&t, 0x80 | 0x0E);  /* latch ch0 tone, freq lo = 0xE */
        PSG_DoCommand(&t, 0x01);         /* freq hi bits */
        PSG_DoCommand(&t, 0x90 | 0x00);  /* latch ch0 volume = 0 (loudest) */
        PSG_DoCommand(&t, 0xE3);         /* latch ch3 noise: white, mode 3 */
        PSG_DoCommand(&t, 0xF0);         /* noise volume = 0 (loudest) */
        std::vector<int16_t> b(4000, 0);
        PSG_Update(&t, b.data(), 2000);
        double s = 0; for (int16_t v : b) s += (double)v * v;
        fprintf(stderr, "[selftest] clownmdemu PSG RMS over 2000 frames = %.0f\n", std::sqrt(s / b.size()));
    }

    /* Parse the ring (chip_trace.c format):
     *   f=<vint> sl=<line> FM  p<0..3> $VV mc=<cycle> wf=<wall> pcz=$ZZZZ
     *   f=<vint> sl=<line> PSG $VV mc=<cycle> wf=<wall> pcz=$ZZZZ
     * mc is the WITHIN-FRAME stamp the mixer sorts by; wf is the wall frame
     * it drains in. Capture order is not stamp order — mixer_order() below
     * reconstructs the order the synth actually saw (mandatory; see
     * chip_ring_decode.py). Old-format lines without mc=/wf= fall back to the
     * legacy (f, sl)-derived timeline in capture order. */
    std::vector<RawEvent> raw; raw.reserve(300000);
    std::vector<Event> legacy; legacy.reserve(300000);
    char line[256];
    while (fgets(line, sizeof line, f)) {
        unsigned fr, sl, port, val, mc, wf;
        if (sscanf(line, "f=%u sl=%u FM p%u $%x mc=%u wf=%u", &fr, &sl, &port, &val, &mc, &wf) == 6) {
            RawEvent r; r.wf = wf; r.mc = mc; r.idx = (uint32_t)raw.size();
            r.kind = 0; r.port = (uint8_t)port; r.val = (uint8_t)val; raw.push_back(r);
        } else if (sscanf(line, "f=%u sl=%u PSG $%x mc=%u wf=%u", &fr, &sl, &val, &mc, &wf) == 5) {
            RawEvent r; r.wf = wf; r.mc = mc; r.idx = (uint32_t)raw.size();
            r.kind = 1; r.port = 0; r.val = (uint8_t)val; raw.push_back(r);
        } else if (sscanf(line, "f=%u sl=%u FM p%u $%x", &fr, &sl, &port, &val) == 4) {
            Event e; e.mc = (uint64_t)fr * MASTER_PER_FRAME + (uint64_t)sl * MASTER_PER_LINE;
            e.kind = 0; e.port = (uint8_t)port; e.val = (uint8_t)val; legacy.push_back(e);
        } else if (sscanf(line, "f=%u sl=%u PSG $%x", &fr, &sl, &val) == 3) {
            Event e; e.mc = (uint64_t)fr * MASTER_PER_FRAME + (uint64_t)sl * MASTER_PER_LINE;
            e.kind = 1; e.port = 0; e.val = (uint8_t)val; legacy.push_back(e);
        }
    }
    fclose(f);
    std::vector<Event> ev = raw.empty() ? legacy : mixer_order(raw);
    fprintf(stderr, "parsed %zu chip-write events from %s (%s order)\n",
            ev.size(), path, raw.empty() ? "legacy capture" : "mixer");
    if (ev.empty()) return 1;

    if (fm_channel >= 0) {
        ev = filter_fm_channel(ev, fm_channel);
        fprintf(stderr, "[--fm-channel %d] filtered stream: %zu events\n",
                fm_channel, ev.size());
        if (ev.empty()) return 1;
    }

    std::vector<int16_t> fm_o, psg_o, fm_t, psg_t;
    render_ours(ev, fm_o, psg_o);
    render_theirs(ev, fm_t, psg_t);
    fprintf(stderr, "ours:   FM %zu PSG %zu | theirs: FM %zu PSG %zu samples\n",
            fm_o.size()/2, psg_o.size(), fm_t.size()/2, psg_t.size());

    /* Decompose: FM-core vs PSG-core, separating LEVEL (best-fit scale) from
     * CONTENT (residual after scaling). Buffers are equal-length per synth. */
    auto compare = [](const char *tag, const std::vector<int16_t>&a, const std::vector<int16_t>&b, int stride){
        size_t n = std::min(a.size(), b.size());
        double sa=0, sb=0;
        for (size_t i=0;i<n;i++){ double x=a[i], y=b[i]; sa+=x*x; sb+=y*y; }
        double ra=std::sqrt(sa/n), rb=std::sqrt(sb/n);
        /* Phase-invariant envelope comparison: per-window RMS (per channel),
         * then Pearson correlation + mean relative error of the envelopes.
         * Square waves drift in phase, so sample-diff is meaningless; envelope
         * (energy over time) captures "is the right sound present + as loud". */
        size_t win = 1024 * (size_t)stride;             /* ~ stride frames */
        std::vector<double> ea, eb;
        for (size_t s=0; s+win<=n; s+=win) {
            double pa=0, pb=0;
            for (size_t i=s;i<s+win;i++){ pa+=(double)a[i]*a[i]; pb+=(double)b[i]*b[i]; }
            ea.push_back(std::sqrt(pa/win)); eb.push_back(std::sqrt(pb/win));
        }
        size_t m=ea.size();
        double ma=0,mb=0; for(size_t i=0;i<m;i++){ma+=ea[i];mb+=eb[i];} ma/=m; mb/=m;
        double cov=0,va=0,vb=0,rel=0;
        for(size_t i=0;i<m;i++){double da=ea[i]-ma,db=eb[i]-mb; cov+=da*db; va+=da*da; vb+=db*db;
            double denom=(ea[i]+eb[i])*0.5+1; rel+=std::fabs(ea[i]-eb[i])/denom;}
        double corr=(va>0&&vb>0)?cov/std::sqrt(va*vb):0; rel/=m;
        printf("  %-4s RMS ours=%.0f theirs=%.0f | envelope corr=%.3f  meanRelErr=%.2f  (1.0/0.0 = perfect)\n",
               tag, ra, rb, corr, rel);
    };
    printf("== core decomposition (phase-invariant envelope match) ==\n");
    compare("FM",  fm_o,  fm_t, 2);   /* stereo interleaved */
    compare("PSG", psg_o, psg_t, 1);  /* mono */

    /* Separate per-stream WAVs for offline spectral analysis. */
    write_wav("ours_fm.wav",   fm_o, 53267);
    write_wav("theirs_fm.wav", fm_t, 53267);
    { std::vector<int16_t> a(psg_o.size()*2), b(psg_t.size()*2);
      for (size_t i=0;i<psg_o.size();i++){a[i*2]=a[i*2+1]=psg_o[i];}
      for (size_t i=0;i<psg_t.size();i++){b[i*2]=b[i*2+1]=psg_t[i];}
      write_wav("ours_psg.wav", a, 223721); write_wav("theirs_psg.wav", b, 223721); }

    std::vector<int16_t> mix_o = mix(fm_o, psg_o);
    std::vector<int16_t> mix_t = mix(fm_t, psg_t);

    /* Align lengths and build diff. */
    size_t n = std::min(mix_o.size(), mix_t.size());
    std::vector<int16_t> diff(n);
    for (size_t i = 0; i < n; i++) diff[i] = clamp16((int32_t)mix_o[i] - (int32_t)mix_t[i]);

    write_wav("ours.wav",   mix_o, 223721);
    write_wav("theirs.wav", mix_t, 223721);
    write_wav("diff.wav",   diff, 223721);

    /* Per-window divergence (0.25 s windows, left channel). */
    size_t frames = n / 2;
    size_t win = 223721 / 4;
    printf("== synth parity report (%.1f s, window 0.25 s) ==\n", frames / 223721.0);
    printf("overall: RMS ours=%.0f theirs=%.0f diff=%.0f\n",
           rms(mix_o, 0, frames*2, 2, 0), rms(mix_t, 0, frames*2, 2, 0), rms(diff, 0, frames*2, 2, 0));
    struct W { double dr, orr, tr; size_t start; };
    std::vector<W> ws;
    for (size_t s = 0; s + win <= frames; s += win) {
        W w; w.start = s;
        w.dr  = rms(diff,  s*2, (s+win)*2, 2, 0);
        w.orr = rms(mix_o, s*2, (s+win)*2, 2, 0);
        w.tr  = rms(mix_t, s*2, (s+win)*2, 2, 0);
        ws.push_back(w);
    }
    std::sort(ws.begin(), ws.end(), [](const W&a, const W&b){ return a.dr > b.dr; });
    printf("top 15 windows by divergence (ours vs theirs):\n");
    printf("  %8s  %8s  %8s  %8s\n", "t(s)", "diffRMS", "oursRMS", "theirsRMS");
    for (size_t i = 0; i < ws.size() && i < 15; i++)
        printf("  %8.2f  %8.0f  %8.0f  %8.0f\n",
               ws[i].start / 223721.0, ws[i].dr, ws[i].orr, ws[i].tr);
    printf("wrote ours.wav theirs.wav diff.wav\n");
    return 0;
}
