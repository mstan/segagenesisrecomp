/* color_lut.c — see color_lut.h. Present-time only.
 *
 * Colorimetry core ported from JRickey/gba-recomp
 * (crates/screen/src/{color,profile,lut}.rs) via the gbarecomp C++ port
 * (src/runtime/color_lut.cpp), © Jrickey, MIT OR Apache-2.0, used with
 * permission. The CIE functions (xy_to_xyz, rgb_to_xyz, bradford_adaptation,
 * rgb_to_rgb, srgb_oetf, matrix helpers) are a verbatim C transliteration of
 * that core. The Genesis 9-bit index, CRT primaries, and ARGB8888 output are
 * this port's. See THIRD-PARTY-LICENSES.md.
 */

#include "color_lut.h"

#include <math.h>
#include <string.h>

/* ── CIE colorimetry (all double; runs only at LUT-build time) ───────── */
typedef struct { double x, y; } Xy;
typedef struct { Xy red, green, blue, white; } Primaries;
typedef struct { double m[3][3]; } Mat3;

/* D65 white point (0.3127, 0.3290) is inlined into each Primaries below. */
static const Primaries kSrgb = {{0.64, 0.33}, {0.30, 0.60}, {0.15, 0.06},
                                {0.3127, 0.3290}};

/* CRT phosphor primaries. Generic SMPTE-C / NTSC-late ("North American
 * broadcast monitor") set — a reasonable, widely-cited CRT gamut. The
 * Trinitron variant uses the commonly-published Sony P22 phosphor chromaticities.
 * These are documented reference values, not hardware we measured ourselves;
 * the model is a plausible CRT, see SHADOW_ENHANCEMENTS.md. */
static const Primaries kPanelSmpteC = {
    {0.630, 0.340}, {0.310, 0.595}, {0.155, 0.070}, {0.3127, 0.3290}};
static const Primaries kPanelTrinitron = {
    {0.625, 0.340}, {0.280, 0.595}, {0.155, 0.070}, {0.3127, 0.3290}};

static bool xy_eq(Xy a, Xy b) { return a.x == b.x && a.y == b.y; }

static void mat_apply(const Mat3* a, const double v[3], double out[3]) {
  for (int i = 0; i < 3; ++i)
    out[i] = a->m[i][0] * v[0] + a->m[i][1] * v[1] + a->m[i][2] * v[2];
}

static Mat3 mat_mul(const Mat3* a, const Mat3* b) {
  Mat3 r;
  memset(&r, 0, sizeof(r));
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      for (int k = 0; k < 3; ++k) r.m[i][j] += a->m[i][k] * b->m[k][j];
  return r;
}

static double cofactor(const Mat3* a, int r, int c) {
  int r1 = (r + 1) % 3, r2 = (r + 2) % 3;
  int c1 = (c + 1) % 3, c2 = (c + 2) % 3;
  return a->m[r1][c1] * a->m[r2][c2] - a->m[r1][c2] * a->m[r2][c1];
}

static Mat3 mat_inverse(const Mat3* a) {
  double det = a->m[0][0] * cofactor(a, 0, 0) +
               a->m[0][1] * cofactor(a, 0, 1) +
               a->m[0][2] * cofactor(a, 0, 2);
  Mat3 out;
  memset(&out, 0, sizeof(out));
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) out.m[i][j] = cofactor(a, j, i) / det;
  return out;
}

static void xy_to_xyz(Xy c, double out[3]) {
  out[0] = c.x / c.y;
  out[1] = 1.0;
  out[2] = (1.0 - c.x - c.y) / c.y;
}

/* Linear RGB → CIE XYZ for a set of primaries (white maps to Y=1). */
static Mat3 rgb_to_xyz(const Primaries* p) {
  double r[3], g[3], b[3], w[3];
  xy_to_xyz(p->red, r);
  xy_to_xyz(p->green, g);
  xy_to_xyz(p->blue, b);
  xy_to_xyz(p->white, w);
  Mat3 m = {{{r[0], g[0], b[0]}, {r[1], g[1], b[1]}, {r[2], g[2], b[2]}}};
  Mat3 minv = mat_inverse(&m);
  double s[3];
  mat_apply(&minv, w, s);
  Mat3 out = m;
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) out.m[i][j] *= s[j];
  return out;
}

static Mat3 bradford_adaptation(Xy from, Xy to) {
  static const Mat3 kBradford = {{{0.8951, 0.2664, -0.1614},
                                  {-0.7502, 1.7135, 0.0367},
                                  {0.0389, -0.0685, 1.0296}}};
  double f[3], t[3], src[3], dst[3];
  xy_to_xyz(from, f);
  xy_to_xyz(to, t);
  mat_apply(&kBradford, f, src);
  mat_apply(&kBradford, t, dst);
  Mat3 scale = {{{dst[0] / src[0], 0, 0},
                 {0, dst[1] / src[1], 0},
                 {0, 0, dst[2] / src[2]}}};
  Mat3 binv = mat_inverse(&kBradford);
  Mat3 a = mat_mul(&binv, &scale);
  return mat_mul(&a, &kBradford);
}

static Mat3 rgb_to_rgb(const Primaries* src, const Primaries* dst) {
  Mat3 to_xyz = rgb_to_xyz(src);
  Mat3 dst_xyz = rgb_to_xyz(dst);
  Mat3 from_xyz = mat_inverse(&dst_xyz);
  if (xy_eq(src->white, dst->white)) return mat_mul(&from_xyz, &to_xyz);
  Mat3 adapt = bradford_adaptation(src->white, dst->white);
  Mat3 a = mat_mul(&from_xyz, &adapt);
  return mat_mul(&a, &to_xyz);
}

static double srgb_oetf(double v) {
  return v <= 0.0031308 ? 12.92 * v : 1.055 * pow(v, 1.0 / 2.4) - 0.055;
}

/* ── CRT optical model ──────────────────────────────────────────────── */
typedef struct {
  Primaries primaries;
  double gamma;        /* CRT display gamma (decode exponent) */
  double luminance;    /* peak white scale */
  double black_floor;  /* lifted black (CRT bloom/veiling glare) */
} PanelModel;

static double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

static double effective_gamma(double base, double darken) {
  darken = clamp01(darken);
  return base + 1.0 * darken;
}

static double default_darken(ScreenKind s) {
  (void)s;
  return 0.0;
}

/* Returns false for RAW/LINEAR (handled separately). */
static bool panel_model(ScreenKind s, double darken, PanelModel* out) {
  switch (s) {
    case SCREEN_CRT:
      out->primaries = kPanelSmpteC;
      out->gamma = effective_gamma(2.4, darken);  /* CRT EOTF ≈ 2.4 */
      out->luminance = 0.95;
      out->black_floor = 0.010;  /* CRT never reaches true black */
      return true;
    case SCREEN_TRINITRON:
      out->primaries = kPanelTrinitron;
      out->gamma = effective_gamma(2.4, darken);
      out->luminance = 1.0;
      out->black_floor = 0.006;
      return true;
    case SCREEN_COMPOSITE:
      /* Composite signal chain softens contrast and warms slightly; modeled
       * only as a gentler gamma + higher black floor. Spatial composite
       * artifacts (dot crawl, rainbows) are NOT synthesized here. */
      out->primaries = kPanelSmpteC;
      out->gamma = effective_gamma(2.2, darken);
      out->luminance = 0.92;
      out->black_floor = 0.020;
      return true;
    default:
      return false;
  }
}

static uint8_t quantize(double v) {
  v = clamp01(v);
  return (uint8_t)(v * 255.0 + 0.5);
}

bool screen_kind_from_name(const char* name, ScreenKind* out) {
  if (!name) return false;
  if (!strcmp(name, "raw"))       { *out = SCREEN_RAW;       return true; }
  if (!strcmp(name, "crt"))       { *out = SCREEN_CRT;       return true; }
  if (!strcmp(name, "trinitron")) { *out = SCREEN_TRINITRON; return true; }
  if (!strcmp(name, "composite")) { *out = SCREEN_COMPOSITE; return true; }
  if (!strcmp(name, "linear"))    { *out = SCREEN_LINEAR;    return true; }
  return false;
}

void color_lut_build(ColorLut* lut, ScreenKind screen, double darken) {
  /* RAW: exact replica of the runtime color conversion (authentic Genesis DAC
   * ladder, normal level). MUST match so default-off output equals the raw
   * framebuffer path. */
  if (screen == SCREEN_RAW) {
    lut->passthrough = true;
    for (int idx = 0; idx < 512; ++idx) {
      uint32_t r = genesis_dac_level(idx & 7u, GENESIS_DAC_NORMAL);
      uint32_t g = genesis_dac_level((idx >> 3) & 7u, GENESIS_DAC_NORMAL);
      uint32_t b = genesis_dac_level((idx >> 6) & 7u, GENESIS_DAC_NORMAL);
      lut->table[idx] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
    return;
  }

  lut->passthrough = false;

  if (screen == SCREEN_LINEAR) {
    /* Decode the ×36 levels as sRGB, expand to linear, re-encode sRGB at full
     * range — a clean reference with no panel coloration. */
    for (int idx = 0; idx < 512; ++idx) {
      double ch[3] = {(idx & 7) / 7.0, ((idx >> 3) & 7) / 7.0,
                      ((idx >> 6) & 7) / 7.0};
      uint32_t out[3];
      for (int i = 0; i < 3; ++i) {
        double lin = pow(ch[i], 2.2);
        out[i] = quantize(srgb_oetf(lin));
      }
      lut->table[idx] = 0xFF000000u | (out[0] << 16) | (out[1] << 8) | out[2];
    }
    return;
  }

  double dk = darken < 0.0 ? default_darken(screen) : darken;
  PanelModel model;
  memset(&model, 0, sizeof(model));
  panel_model(screen, dk, &model);
  Mat3 to_display = rgb_to_rgb(&model.primaries, &kSrgb);

  for (int idx = 0; idx < 512; ++idx) {
    double c[3] = {(idx & 7) / 7.0, ((idx >> 3) & 7) / 7.0,
                   ((idx >> 6) & 7) / 7.0};
    double lin[3];
    for (int i = 0; i < 3; ++i) {
      double v = pow(c[i], model.gamma) * model.luminance;
      lin[i] = v > 1.0 ? 1.0 : v;
    }
    double out[3];
    mat_apply(&to_display, lin, out);
    uint32_t q[3];
    for (int i = 0; i < 3; ++i) {
      double v = clamp01(out[i]);
      double lifted = model.black_floor + (1.0 - model.black_floor) * v;
      q[i] = quantize(srgb_oetf(lifted));
    }
    lut->table[idx] = 0xFF000000u | (q[0] << 16) | (q[1] << 8) | q[2];
  }
}

void color_lut_map_frame(const ColorLut* lut, const uint32_t* src,
                         uint32_t* dst, int width, int height, int stride_px) {
  for (int y = 0; y < height; ++y) {
    const uint32_t* srow = src + (size_t)y * stride_px;
    uint32_t* drow = dst + (size_t)y * stride_px;
    for (int x = 0; x < width; ++x)
      drow[x] = color_lut_map_argb(lut, srow[x]);
  }
}
