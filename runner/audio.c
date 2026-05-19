#include "audio.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

/* Volume divisors from the reference mixer (clownmdemu-frontend-common/mixer.h) */
#define FM_VOL_DIV  1
#define PSG_VOL_DIV 8

static SDL_AudioDeviceID s_dev = 0;

/* Audio stats */
static AudioStats s_stats = {0};

/* WAV capture state */
static FILE *s_wav_file = NULL;
static uint32_t s_wav_data_bytes = 0;

/* Scratch output buffer — sized for worst-case PSG frames per video frame.
 * PSG rate ~223721 Hz / 50 Hz (PAL) ≈ 4475 frames.  Double for headroom. */
#define OUT_BUF_FRAMES 16384
static int16_t s_out[OUT_BUF_FRAMES * 2];

int audio_init(int psg_sample_rate)
{
    SDL_AudioSpec want, got;
    SDL_memset(&want, 0, sizeof(want));
    want.freq     = psg_sample_rate;
    want.format   = AUDIO_S16SYS;
    want.channels = 2;
    want.samples  = 1024;
    want.callback = NULL;   /* push model via SDL_QueueAudio */

    s_dev = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
    if (s_dev == 0) {
        fprintf(stderr, "audio: SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        return -1;
    }
    SDL_PauseAudioDevice(s_dev, 0);
    return 0;
}

void audio_close(void)
{
    if (s_dev != 0) {
        SDL_CloseAudioDevice(s_dev);
        s_dev = 0;
    }
}

void audio_flush(const int16_t *fm_buf,  size_t fm_frames,
                 const int16_t *psg_buf, size_t psg_frames)
{
    if (s_dev == 0 || psg_buf == NULL || psg_frames == 0)
        return;

    if (psg_frames > OUT_BUF_FRAMES)
        psg_frames = OUT_BUF_FRAMES;

    /* Reference mixer approach: iterate at PSG rate (no PSG resampling),
     * upsample FM to PSG rate via nearest-neighbour.
     * Divisors: PSG/8, FM/1 — same as clownmdemu-frontend-common/mixer.h */
    for (size_t i = 0; i < psg_frames; i++) {
        int32_t p = (int32_t)psg_buf[i] / PSG_VOL_DIV;
        int32_t l = p;
        int32_t r = p;

        if (fm_buf != NULL && fm_frames > 0) {
            size_t fi = i * fm_frames / psg_frames;
            l += (int32_t)fm_buf[fi * 2 + 0] / FM_VOL_DIV;
            r += (int32_t)fm_buf[fi * 2 + 1] / FM_VOL_DIV;
        }

        if (l >  32767) l =  32767; else if (l < -32768) l = -32768;
        if (r >  32767) r =  32767; else if (r < -32768) r = -32768;

        s_out[i * 2 + 0] = (int16_t)l;
        s_out[i * 2 + 1] = (int16_t)r;
    }

    /* Hard cap on queue depth to prevent unbounded drift accumulation.
     * If the queue is already above ~3 frames worth, drop this frame's
     * samples rather than queue them. Brief audible glitch beats the
     * minutes-long lag that builds up without a hard cap. */
    Uint32 frame_bytes = (Uint32)(psg_frames * 2 * sizeof(int16_t));
    Uint32 queued = SDL_GetQueuedAudioSize(s_dev);
    Uint32 limit = frame_bytes * 3;
    if (queued > limit) {
        s_stats.dropped_flushes++;
    } else {
        SDL_QueueAudio(s_dev, s_out, frame_bytes);
    }

    /* WAV capture */
    if (s_wav_file) {
        uint32_t bytes = (uint32_t)(psg_frames * 2 * sizeof(int16_t));
        fwrite(s_out, 1, bytes, s_wav_file);
        s_wav_data_bytes += bytes;
    }

    /* Stats */
    s_stats.last_fm_frames  = fm_frames;
    s_stats.last_psg_frames = psg_frames;
    s_stats.total_fm_frames  += fm_frames;
    s_stats.total_psg_frames += psg_frames;
    s_stats.total_flushes++;
}

/* =========================================================================
 * WAV capture
 * ========================================================================= */

static void wav_write_header(FILE *f, uint32_t sample_rate, uint32_t data_bytes)
{
    uint32_t total = 36 + data_bytes;
    uint16_t channels = 2;
    uint16_t bits = 16;
    uint32_t byte_rate = sample_rate * channels * (bits / 8);
    uint16_t block_align = channels * (bits / 8);

    fwrite("RIFF", 1, 4, f);
    fwrite(&total, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    uint32_t fmt_size = 16;
    fwrite(&fmt_size, 4, 1, f);
    uint16_t pcm = 1;
    fwrite(&pcm, 2, 1, f);
    fwrite(&channels, 2, 1, f);
    fwrite(&sample_rate, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&block_align, 2, 1, f);
    fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&data_bytes, 4, 1, f);
}

int audio_wav_start(const char *path)
{
    if (s_wav_file) audio_wav_stop();
    s_wav_file = fopen(path, "wb");
    if (!s_wav_file) return -1;
    s_wav_data_bytes = 0;
    /* Write placeholder header — will be updated on stop */
    wav_write_header(s_wav_file, 223721, 0);
    fprintf(stderr, "[audio] WAV capture started: %s\n", path);
    return 0;
}

void audio_wav_stop(void)
{
    if (!s_wav_file) return;
    /* Rewrite header with correct data size */
    fseek(s_wav_file, 0, SEEK_SET);
    wav_write_header(s_wav_file, 223721, s_wav_data_bytes);
    fclose(s_wav_file);
    s_wav_file = NULL;
    fprintf(stderr, "[audio] WAV capture stopped: %u bytes\n", s_wav_data_bytes);
}

int audio_wav_active(void) { return s_wav_file != NULL; }

void audio_get_stats(AudioStats *out) { *out = s_stats; }
uint32_t audio_queued_bytes(void) { return s_dev ? SDL_GetQueuedAudioSize(s_dev) : 0; }
