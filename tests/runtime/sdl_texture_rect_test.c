#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_W = 512, MAX_H = 480, OUT_W = 973, OUT_H = 541 };

typedef struct CaseDef {
    const char *name;
    int w;
    int h;
    int crop_y;
    int crop_h;
} CaseDef;

static uint32_t framebuf[MAX_W * MAX_H];
static uint32_t initbuf[MAX_W * MAX_H];
static uint32_t read_a[OUT_W * OUT_H];
static uint32_t read_b[OUT_W * OUT_H];

static uint32_t pixel_value(int frame, int x, int y)
{
    uint32_t r = (uint32_t)((x * 3 + frame * 17) & 0xff);
    uint32_t g = (uint32_t)((y * 5 + frame * 29) & 0xff);
    uint32_t b = (uint32_t)(((x ^ y) * 7 + frame * 11) & 0xff);
    return 0xff000000u | (r << 16) | (g << 8) | b;
}

static void fill_init(void)
{
    for (int y = 0; y < MAX_H; y++) {
        for (int x = 0; x < MAX_W; x++) {
            initbuf[y * MAX_W + x] =
                0xff000000u | (uint32_t)((x * 13) & 0xff) << 16 |
                (uint32_t)((y * 9) & 0xff) << 8 |
                (uint32_t)((x + y) & 0xff);
        }
    }
}

static void fill_frame(int frame, int w, int h)
{
    uint32_t poison = 0xff000000u | (uint32_t)((frame * 37) & 0xff) << 16 |
                      (uint32_t)((frame * 53) & 0xff) << 8 |
                      (uint32_t)((frame * 71) & 0xff);
    for (int y = 0; y < MAX_H; y++) {
        for (int x = 0; x < MAX_W; x++) {
            framebuf[y * MAX_W + x] =
                (x < w && y < h) ? pixel_value(frame, x, y) : poison;
        }
    }
}

static int read_render(SDL_Renderer *renderer, SDL_Texture *texture,
                       const SDL_Rect *src, uint32_t *out)
{
    SDL_SetRenderDrawColor(renderer, 3, 5, 7, 255);
    if (SDL_RenderClear(renderer) != 0) return -1;
    if (SDL_RenderCopy(renderer, texture, src, NULL) != 0) return -1;
    if (SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
                             out, OUT_W * (int)sizeof(uint32_t)) != 0) {
        return -1;
    }
    return 0;
}

static int compare_pixels(const uint32_t *a, const uint32_t *b,
                          const char *label)
{
    int mismatches = 0;
    for (int i = 0; i < OUT_W * OUT_H; i++) {
        if (a[i] != b[i]) {
            if (mismatches < 8) {
                int x = i % OUT_W;
                int y = i / OUT_W;
                fprintf(stderr,
                        "%s mismatch at %d,%d: full=%08x rect=%08x\n",
                        label, x, y, a[i], b[i]);
            }
            mismatches++;
        }
    }
    return mismatches;
}

static uint64_t perf_counter(void)
{
    return SDL_GetPerformanceCounter();
}

static double seconds_since(uint64_t start, uint64_t end)
{
    return (double)(end - start) / (double)SDL_GetPerformanceFrequency();
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    const char *driver_hint = getenv("SDL_RECT_TEST_RENDER_DRIVER");
    const char *software = getenv("SDL_RECT_TEST_SOFTWARE");
    const char *raw_linear_rect = getenv("SDL_RECT_TEST_RAW_LINEAR_RECT");
    const char *timing = getenv("SDL_RECT_TEST_TIMING");
    if (driver_hint && driver_hint[0])
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, driver_hint);
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 2;
    }

    SDL_Window *window = SDL_CreateWindow("texture-rect-test",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        OUT_W, OUT_H, SDL_WINDOW_HIDDEN);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 2;
    }
    Uint32 renderer_flags = (software && software[0] && software[0] != '0')
        ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED;
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, renderer_flags);
    if (!renderer) {
        if (renderer_flags != 0)
            renderer = SDL_CreateRenderer(window, -1, 0);
        if (!renderer) {
            fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 2;
        }
    }
    SDL_RendererInfo info;
    SDL_memset(&info, 0, sizeof(info));
    SDL_GetRendererInfo(renderer, &info);
    printf("SDL_RECT_RENDERER name=%s flags=0x%x\n",
           info.name ? info.name : "(unknown)", (unsigned)info.flags);

    fill_init();
    const CaseDef cases[] = {
        { "max-512x480", 512, 480, 0, 480 },
        { "native-320x224", 320, 224, 0, 224 },
        { "h32-256x224", 256, 224, 0, 224 },
        { "h32-v30-256x240", 256, 240, 0, 240 },
        { "v30-320x240", 320, 240, 0, 240 },
        { "wide-448x224", 448, 224, 0, 224 },
        { "wide-max-512x224", 512, 224, 0, 224 },
        { "im2-320x448-full", 320, 448, 0, 448 },
        { "im2-peer-top", 320, 448, 0, 224 },
        { "im2-peer-bottom", 320, 448, 224, 224 },
        { "shrink-back-native", 320, 224, 0, 224 },
        { "grow-back-max", 512, 480, 0, 480 },
    };
    const char *qualities[] = { "0", "1" };
    int failures = 0;

    for (int q = 0; q < 2; q++) {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, qualities[q]);
        SDL_Texture *full = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING, MAX_W, MAX_H);
        SDL_Texture *rect = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING, MAX_W, MAX_H);
        if (!full || !rect) {
            fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
            return 2;
        }
        SDL_SetTextureScaleMode(full, q ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
        SDL_SetTextureScaleMode(rect, q ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
        SDL_UpdateTexture(full, NULL, initbuf, MAX_W * (int)sizeof(uint32_t));
        SDL_UpdateTexture(rect, NULL, initbuf, MAX_W * (int)sizeof(uint32_t));

        for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
            const CaseDef *c = &cases[i];
            SDL_Rect active = { 0, 0, c->w, c->h };
            SDL_Rect src = { 0, c->crop_y, c->w, c->crop_h };
            char label[128];
            fill_frame(i + q * 100, c->w, c->h);
            if (SDL_UpdateTexture(full, NULL, framebuf,
                                  MAX_W * (int)sizeof(uint32_t)) != 0) {
                fprintf(stderr, "full update failed: %s\n", SDL_GetError());
                return 2;
            }
            const SDL_Rect *candidate_rect =
                (q == 1 && !(raw_linear_rect && raw_linear_rect[0] &&
                              raw_linear_rect[0] != '0')) ? NULL : &active;
            if (SDL_UpdateTexture(rect, candidate_rect, framebuf,
                                  MAX_W * (int)sizeof(uint32_t)) != 0) {
                fprintf(stderr, "rect update failed: %s\n", SDL_GetError());
                return 2;
            }
            if (read_render(renderer, full, &src, read_a) != 0 ||
                read_render(renderer, rect, &src, read_b) != 0) {
                fprintf(stderr, "render/read failed: %s\n", SDL_GetError());
                return 2;
            }
            snprintf(label, sizeof(label), "quality=%s case=%s", qualities[q], c->name);
            int mismatches = compare_pixels(read_a, read_b, label);
            if (mismatches) {
                fprintf(stderr, "%s total mismatches=%d\n", label, mismatches);
                failures++;
            }
        }
        SDL_DestroyTexture(rect);
        SDL_DestroyTexture(full);
    }

    if (timing && timing[0] && timing[0] != '0') {
        SDL_Texture *tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING, MAX_W, MAX_H);
        const int iters = 20000;
        SDL_Rect active = { 0, 0, 320, 224 };
        int update_failed = 0;
        fill_frame(777, 320, 224);
        uint64_t t0 = perf_counter();
        for (int i = 0; i < iters; i++) {
            if (SDL_UpdateTexture(tex, NULL, framebuf,
                                  MAX_W * (int)sizeof(uint32_t)) != 0) {
                update_failed = 1;
                break;
            }
        }
        uint64_t t1 = perf_counter();
        for (int i = 0; i < iters && !update_failed; i++) {
            if (SDL_UpdateTexture(tex, &active, framebuf,
                                  MAX_W * (int)sizeof(uint32_t)) != 0) {
                update_failed = 1;
                break;
            }
        }
        uint64_t t2 = perf_counter();
        if (update_failed) {
            fprintf(stderr, "timing update failed: %s\n", SDL_GetError());
            SDL_DestroyTexture(tex);
            return 2;
        }
        double full_s = seconds_since(t0, t1);
        double rect_s = seconds_since(t1, t2);
        printf("SDL_RECT_UPLOAD_TIMING iters=%d full_seconds=%.9f rect_seconds=%.9f ratio=%.4f\n",
               iters, full_s, rect_s, rect_s / full_s);
        SDL_DestroyTexture(tex);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (failures) {
        fprintf(stderr, "SDL texture rect equivalence failed in %d cases\n", failures);
        return 1;
    }
    printf("SDL texture rect equivalence passed\n");
    return 0;
}
