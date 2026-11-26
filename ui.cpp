/*
  ═══════════════════════════════════════════════════════════════
  UI SCREENS AND OVERLAYS — implementation

  1. Text: the 5x7 font drawn as horizontal spans (fill16 bursts, no
     per-pixel calls), with an outline grown around the dots, a drop
     shadow, an italic shear and a vertical gradient. Fractional
     scales work (zooming countdown digits).
  2. Widgets: chamfered panels, the animated selection bar, icons.
  3. Backdrops: the showroom (sky of the current theme, a scrolling
     neon floor grid, a spotlight and the rotating car).
  4. Screens: title, menus, circuit select, countdown, banners,
     finish, results, standings, champion, pause, crash, demo.
  ═══════════════════════════════════════════════════════════════
*/

#pragma GCC optimize("O2")   // the HUD draws text and the minimap every frame

#include "ui.h"
#include "gfx.h"
#include "colors.h"
#include "rendering.h"
#include "render_player.h"
#include "circuits.h"
#include "track.h"
#include "physics.h"
#include "save.h"
#include "race.h"
#include "game.h"
#include "config.h"
#include "board.h"
#include "platform.h"
#include "utils.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static inline float easeOut(float t) { t = clamp01(t); return 1.0f - (1.0f - t) * (1.0f - t); }
static inline float easeOutBack(float t) {
  t = clamp01(t);
  const float c1 = 1.70158f, c3 = c1 + 1.0f;
  float u = t - 1.0f;
  return 1.0f + c3 * u * u * u + c1 * u * u;
}
static inline int alphaOf(float f) { return iclamp((int)(f * 32.0f + 0.5f), 0, 32); }

static const char* placeSuffix(int p) {
  return p == 1 ? "ST" : p == 2 ? "ND" : p == 3 ? "RD" : "TH";
}

static uint16_t placeColor(int p) {
  return p == 1 ? UI_GOLD : p == 2 ? UI_SILVER : p == 3 ? UI_BRONZE : UI_TEXT;
}

static uint16_t paintOf(int racer) {
  uint16_t p = racers[racer].paint;
  return p ? p : UI_RGB(46, 62, 140);          // texture paint (navy)
}

void ui_fmtTime(char* out, int n, float t) {
  if (!(t > 0)) t = 0;
  if (t > 5999) t = 5999;
  int cs = (int)(t * 100.0f + 0.5f);
  snprintf(out, n, "%d:%02d.%02d", cs / 6000, (cs / 100) % 60, cs % 100);
}

// "+1.25" under a minute, "+1:02.11" above
static void fmtGap(char* out, int n, float d) {
  if (!(d > 0)) d = 0;
  if (d < 60.0f) {
    int cs = (int)(d * 100.0f + 0.5f);
    snprintf(out, n, "+%d.%02d", cs / 100, cs % 100);
  } else {
    if (d > 5999) d = 5999;
    int cs = (int)(d * 100.0f + 0.5f);
    snprintf(out, n, "+%d:%02d.%02d", cs / 6000, (cs / 100) % 60, cs % 100);
  }
}

// ═══════════════════════════════════════════════════════════════
//  1. TEXT
//  The glyphs of gfx.cpp (ASCII 32..95, bit 4 = leftmost column),
//  drawn as run-length spans with outlines instead of one call per dot.
// ═══════════════════════════════════════════════════════════════
static inline const uint8_t* glyphOf(char ch) {
  if (ch >= 'a' && ch <= 'z') ch -= 32;
  if (ch < 32 || ch > 95) ch = '?';
  return gfx_font5x7[ch - 32];
}

// Fill [xa, xb) of one framebuffer row, clipped to the clip rect's x range.
// Text runs are short (2..20 px): a plain loop beats a gfx_hspan call.
static inline void runFill(uint16_t* row, int xa, int xb, uint16_t c, int alpha) {
  if (xa < gfx_cx0) xa = gfx_cx0;
  if (xb > gfx_cx1) xb = gfx_cx1;
  if (alpha >= 32) {
    for (int x = xa; x < xb; x++) row[x] = c;
  } else {
    for (int x = xa; x < xb; x++) row[x] = blend565(c, row[x], (uint32_t)alpha);
  }
}

static inline int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

// Runs of set bits of one glyph row (bit 4 = leftmost column): for each
// run [c0, c1] call emit(c0, c1)
#define FOR_RUNS(bits, c0, c1, body)                                         \
  for (int c0 = 0; c0 < 5;) {                                                \
    if (!((bits) & (0x10 >> c0))) { c0++; continue; }                        \
    int c1 = c0;                                                             \
    while (c1 < 4 && ((bits) & (0x10 >> (c1 + 1)))) c1++;                    \
    body;                                                                    \
    c0 = c1 + 1;                                                             \
  }

// One pass over the string: for every pixel row, the union of the glyph
// dots grown by `grow` pixels, filled with colour c or the style gradient.
// Integral scales (everything but zooms) run in integer arithmetic.
static void textPass(float x, float y, const char* s, int n, const UiTextStyle& st,
                     int grow, bool grad, uint16_t c) {
  const float sc = st.scale;
  if (!(sc > 0.05f) || st.alpha == 0) return;
  const float h = 7.0f * sc;
  const bool flat = !grad || (st.g[0] == st.g[1] && st.g[1] == st.g[2] && st.g[2] == st.g[3]);
  uint16_t col = grad ? st.g[0] : c;
  const int alpha = st.alpha;
  const int isc = (int)sc;
  const bool integral = (float)isc == sc && isc > 0;
  int ya = (int)floorf(y - grow), yb = (int)ceilf(y + h + grow);
  if (ya < gfx_cy0) ya = gfx_cy0;
  if (yb > gfx_cy1) yb = gfx_cy1;
  const float inv = 1.0f / sc;
  const int ix = (int)x, iy = (int)y;           // whole pixels (ui_text rounds them)
  const int iadv = 6 * isc + st.spacing;
  const float adv = 6.0f * sc + st.spacing;
  for (int py = ya; py < yb; py++) {
    const float yc = py + 0.5f - y;
    int r0, r1;
    if (integral) {
      r0 = floorDiv(py - iy - grow, isc);
      r1 = floorDiv(py - iy + grow, isc);
    } else {
      r0 = (int)floorf((yc - grow) * inv - 1.0f) + 1;
      r1 = (int)floorf((yc + grow) * inv);
    }
    if (r0 < 0) r0 = 0;
    if (r1 > 6) r1 = 6;
    if (r0 > r1) continue;
    if (!flat) {
      float tt = yc / h;
      col = tt < 0.5f ? lerpCol(st.g[0], st.g[1], tt * 2.0f) : lerpCol(st.g[2], st.g[3], tt * 2.0f - 1.0f);
    }
    uint16_t* row = gfx_fb + py * SCR_W;
    const float shear = st.slant * (h - yc);
    if (integral) {
      // round the shear (it is slightly negative on outline rows under the box)
      int gx = ix + (st.slant != 0.0f ? (int)(shear + 16.5f) - 16 : 0);
      for (int i = 0; i < n; i++, gx += iadv) {
        const uint8_t* g = glyphOf(s[i]);
        uint8_t bits = 0;
        for (int r = r0; r <= r1; r++) bits |= g[r];
        if (!bits) continue;
        FOR_RUNS(bits, c0, c1, runFill(row, gx + c0 * isc - grow, gx + (c1 + 1) * isc + grow, col, alpha));
      }
    } else {
      for (int i = 0; i < n; i++) {
        const uint8_t* g = glyphOf(s[i]);
        uint8_t bits = 0;
        for (int r = r0; r <= r1; r++) bits |= g[r];
        if (!bits) continue;
        const float gx = x + i * adv + shear;
        FOR_RUNS(bits, c0, c1,
                 runFill(row, (int)ceilf(gx + c0 * sc - grow - 0.5f), (int)ceilf(gx + (c1 + 1) * sc + grow - 0.5f),
                         col, alpha));
      }
    }
  }
}

int ui_textWidth(const char* s, float scale, int spacing) {
  int n = (int)strlen(s);
  if (n <= 0) return 0;
  return (int)(n * (6.0f * scale + spacing) - scale - spacing + 0.5f);
}

void ui_text(float x, float y, const char* s, const UiTextStyle& st, int align) {
  int n = (int)strlen(s);
  if (n <= 0 || st.alpha == 0) return;
  const float w = n * (6.0f * st.scale + st.spacing) - st.scale - st.spacing;
  const float shearW = st.slant * 7.0f * st.scale;
  if (align == UI_CENTER) x -= (w + shearW) * 0.5f;
  else if (align == UI_RIGHT) x -= w + shearW;
  // whole pixels keep integral scales crisp
  x = floorf(x + 0.5f);
  y = floorf(y + 0.5f);
  if (st.shadowDx || st.shadowDy)
    textPass(x + st.shadowDx, y + st.shadowDy, s, n, st, st.outlinePx, false, st.shadow);
  if (st.outlinePx) textPass(x, y, s, n, st, st.outlinePx, false, st.outline);
  textPass(x, y, s, n, st, 0, true, st.g[0]);
}

UiTextStyle ui_style(float scale, uint16_t c) {
  UiTextStyle s;
  s.scale = scale;
  s.slant = 0;
  s.spacing = 0;
  s.outlinePx = 1;
  s.shadowDx = 0;
  s.shadowDy = 0;
  s.alpha = 32;
  s.g[0] = s.g[1] = s.g[2] = s.g[3] = c;
  s.outline = UI_SHADOW;
  s.shadow = UI_SHADOW;
  return s;
}

UiTextStyle ui_styleGrad(float scale, uint16_t top, uint16_t bottom) {
  UiTextStyle s = ui_style(scale, top);
  uint16_t mid = lerpCol(top, bottom, 0.5f);
  s.g[0] = top; s.g[1] = mid; s.g[2] = mid; s.g[3] = bottom;
  s.slant = 0.16f;
  s.outlinePx = scale >= 4.0f ? 2 : 1;
  int sh = scale >= 3.0f ? 2 : 1;
  s.shadowDx = (int8_t)sh;
  s.shadowDy = (int8_t)sh;
  return s;
}

UiTextStyle ui_styleChrome(float scale) {
  UiTextStyle s = ui_styleGrad(scale, COL_WHITE, COL_WHITE);
  s.g[0] = UI_RGB(255, 255, 255);
  s.g[1] = UI_RGB(110, 170, 255);
  s.g[2] = UI_RGB(255, 236, 190);
  s.g[3] = UI_RGB(255, 96, 140);
  s.outline = UI_RGB(14, 8, 40);
  s.shadow = UI_RGB(0, 0, 0);
  return s;
}

UiTextStyle ui_styleGold(float scale) {
  UiTextStyle s = ui_styleGrad(scale, COL_WHITE, COL_WHITE);
  s.g[0] = UI_RGB(255, 255, 214);
  s.g[1] = UI_RGB(255, 214, 70);
  s.g[2] = UI_RGB(255, 196, 40);
  s.g[3] = UI_RGB(214, 96, 16);
  s.outline = UI_RGB(40, 16, 0);
  s.shadow = UI_RGB(0, 0, 0);
  return s;
}

static UiTextStyle styleHot(float scale) {
  UiTextStyle s = ui_styleGrad(scale, COL_WHITE, COL_WHITE);
  s.g[0] = UI_RGB(255, 236, 120);
  s.g[1] = UI_RGB(255, 120, 60);
  s.g[2] = UI_RGB(255, 90, 60);
  s.g[3] = UI_RGB(200, 20, 70);
  s.outline = UI_RGB(40, 0, 16);
  return s;
}

static UiTextStyle styleGreen(float scale) {
  UiTextStyle s = ui_styleGrad(scale, COL_WHITE, COL_WHITE);
  s.g[0] = UI_RGB(230, 255, 210);
  s.g[1] = UI_RGB(90, 255, 120);
  s.g[2] = UI_RGB(60, 230, 100);
  s.g[3] = UI_RGB(0, 140, 70);
  s.outline = UI_RGB(0, 36, 10);
  return s;
}

void ui_label(int x, int y, const char* s, uint16_t c, int scale, int align) {
  UiTextStyle st = ui_style((float)scale, c);
  ui_text((float)x, (float)y, s, st, align);
}

// Label with alpha (pulsing prompts)
static void labelA(int x, int y, const char* s, uint16_t c, int scale, int align, int alpha) {
  UiTextStyle st = ui_style((float)scale, c);
  st.alpha = (uint8_t)iclamp(alpha, 0, 32);
  ui_text((float)x, (float)y, s, st, align);
}

// ═══════════════════════════════════════════════════════════════
//  2. WIDGETS
// ═══════════════════════════════════════════════════════════════

// Shading: darken a span towards the panel ink with shifts and masks, two
// pixels per 32-bit word (~4x cheaper than a generic alpha blend; the HUD
// shades ~20 000 pixels per frame). Levels keep 1/2, 3/8 or 1/4 of the
// background (like alpha 16 / 20 / 24 of UI_INK).
typedef uint32_t __attribute__((__may_alias__)) u32ui;
// UI_INK scaled by num/den per channel (what the shade adds back)
static constexpr uint32_t inkScaled(uint32_t num, uint32_t den) {
  return (((((uint32_t)UI_INK >> 11) & 31u) * num / den) << 11) |
         (((((uint32_t)UI_INK >> 5) & 63u) * num / den) << 5) | ((((uint32_t)UI_INK) & 31u) * num / den);
}
static const uint32_t INK_H = inkScaled(1, 2);   // level 0 keeps 1/2 of the background
static const uint32_t INK_5 = inkScaled(5, 8);   // level 1 keeps 3/8
static const uint32_t INK_3 = inkScaled(3, 4);   // level 2 keeps 1/4

static inline uint32_t shadeW(uint32_t w, int level) {
  if (level == 0) return ((w >> 1) & 0x7BEF7BEFu) + INK_H * 0x10001u;
  if (level == 1) return ((w >> 2) & 0x39E739E7u) + ((w >> 3) & 0x18E318E3u) + INK_5 * 0x10001u;
  return ((w >> 2) & 0x39E739E7u) + INK_3 * 0x10001u;
}

static void HOT shadeSpan(int y, int xa, int xb, int level) {
  if (y < gfx_cy0 || y >= gfx_cy1) return;
  if (xa < gfx_cx0) xa = gfx_cx0;
  if (xb > gfx_cx1) xb = gfx_cx1;
  int n = xb - xa;
  if (n <= 0) return;
  uint16_t* p = gfx_fb + y * SCR_W + xa;
  if ((uintptr_t)p & 2) { *p = (uint16_t)shadeW(*p, level); p++; n--; }
  u32ui* q = (u32ui*)p;
  for (; n >= 2; n -= 2, q++) *q = shadeW(*q, level);
  if (n) *(uint16_t*)q = (uint16_t)shadeW(*(uint16_t*)q, level);
}

void ui_init() {
  plat_log_iram("ui shadeSpan", (const void*)shadeSpan);
}

static inline int shadeLevel(int alpha) { return alpha <= 16 ? 0 : (alpha <= 21 ? 1 : 2); }

void ui_shadeRect(int x, int y, int w, int h, int alpha) {
  const int lv = shadeLevel(alpha);
  for (int yy = y; yy < y + h; yy++) shadeSpan(yy, x, x + w, lv);
}
#define shadeRect ui_shadeRect

void ui_shadeEllipse(int cx, int cy, int r, int alpha) {
  const int lv = shadeLevel(alpha);
  const float inv = 1.0f / ((float)r + 0.5f);
  for (int dy = -r; dy <= r; dy++) {
    float t = dy * inv, s = 1.0f - t * t;
    if (s <= 0) continue;
    int hw = (int)(((float)r + 0.5f) * sqrtf(s));
    shadeSpan(cy + dy, cx - hw, cx + hw + 1, lv);
  }
}

void ui_panel(int x, int y, int w, int h, uint16_t accent, int alpha) {
  if (w < 8 || h < 8) return;
  const int ic = h >= 48 ? 8 : 5;                  // chamfer of two corners
  // body: rows with the top-left and bottom-right corners cut at 45 degrees
  const int lv = shadeLevel(alpha);
  for (int r = 0; r < h; r++) {
    int xa = x + (r < ic ? ic - r : 0);
    int xb = x + w - (r > h - 1 - ic ? r - (h - 1 - ic) : 0);
    shadeSpan(y + r, xa, xb, lv);
  }
  const int xr = x + w - 1, yb = y + h - 1;
  gfx_line(x + ic, y, xr, y, UI_EDGE);
  gfx_line(xr, y, xr, yb - ic, UI_EDGE);
  gfx_line(xr, yb - ic, xr - ic, yb, UI_EDGE);
  gfx_line(xr - ic, yb, x, yb, UI_EDGE);
  gfx_line(x, yb, x, y + ic, UI_EDGE);
  gfx_line(x, y + ic, x + ic, y, UI_EDGE);
  if (accent) {
    // accent rule over the top edge and the chamfer
    gfx_line(x, y + ic, x + ic, y, accent);
    gfx_line(x + 1, y + ic, x + ic + 1, y, accent);
    gfx_hspan(y, x + ic, x + w / 2, accent);
    gfx_hspan(y + 1, x + ic + 1, x + w / 3, accent);
    gfx_line(xr, yb - ic, xr - ic, yb, accent);
  }
}

void ui_selBar(int x, int y, int w, int h, float t, uint16_t c) {
  if (w < 6 || h < 3) return;
  // body fading to the right
  const int steps = 6;
  for (int i = 0; i < steps; i++) {
    int xa = x + w * i / steps, xb = x + w * (i + 1) / steps;
    gfx_fillRectMode(xa, y, xb - xa, h, c, GFX_BLEND, 15 - i * 2);
  }
  gfx_hspanMode(y, x, x + w, c, GFX_BLEND, 24);
  gfx_hspanMode(y + h - 1, x, x + w, c, GFX_BLEND, 16);
  gfx_fillRect(x, y, 3, h, c);
  // shine sweeping across
  int sx0 = gfx_cx0, sy0 = gfx_cy0, sx1 = gfx_cx1, sy1 = gfx_cy1;
  gfx_setClip(imax(x, sx0), imax(y, sy0), imin(x + w, sx1), imin(y + h, sy1));
  float ph = fmodf(t * 0.65f, 1.0f);
  float sx = x - h + ph * (w + 2.0f * h);
  gfx_fillQuad(sx, (float)y, sx + 9.0f, (float)y, sx + 9.0f - h * 0.6f, (float)(y + h),
               sx - h * 0.6f, (float)(y + h), COL_WHITE, GFX_BLEND, 9);
  gfx_setClip(sx0, sy0, sx1, sy1);
}

void ui_carIcon(int x, int y, uint16_t paint) {
  const uint16_t dark = UI_SHADOW;
  uint16_t shade = darkenCol(paint, 0.62f);
  // outline
  gfx_fillRect(x - 1, y + 3, 20, 5, dark);
  gfx_fillQuad((float)x + 3, (float)y + 4, (float)x + 5.5f, (float)y - 0.5f,
               (float)x + 12.5f, (float)y - 0.5f, (float)x + 16, (float)y + 4, dark);
  // body + cabin
  gfx_fillRect(x, y + 4, 18, 3, paint);
  gfx_hspan(y + 4, x + 1, x + 17, lerpCol(paint, COL_WHITE, 0.35f));
  gfx_fillQuad((float)x + 4, (float)y + 4, (float)x + 6, (float)y + 0.5f,
               (float)x + 12, (float)y + 0.5f, (float)x + 15, (float)y + 4, shade);
  gfx_fillQuad((float)x + 6, (float)y + 3.6f, (float)x + 7, (float)y + 1.4f,
               (float)x + 11.4f, (float)y + 1.4f, (float)x + 13.2f, (float)y + 3.6f, UI_RGB(150, 200, 240));
  // wheels
  gfx_fillCircle(x + 4, y + 7, 2, dark);
  gfx_fillCircle(x + 14, y + 7, 2, dark);
  gfx_pixel(x + 4, y + 7, UI_DIM);
  gfx_pixel(x + 14, y + 7, UI_DIM);
}

void ui_arrow(int cx, int cy, int size, int dir, uint16_t c) {
  float s = (float)size, x = (float)cx, y = (float)cy;
  switch (dir) {
    case 0:  gfx_fillTri(x - s, y + s * 0.6f, x + s, y + s * 0.6f, x, y - s * 0.7f, c); break;
    case 1:  gfx_fillTri(x - s, y - s * 0.6f, x + s, y - s * 0.6f, x, y + s * 0.7f, c); break;
    case 2:  gfx_fillTri(x + s * 0.6f, y - s, x + s * 0.6f, y + s, x - s * 0.7f, y, c); break;
    default: gfx_fillTri(x - s * 0.6f, y - s, x - s * 0.6f, y + s, x + s * 0.7f, y, c); break;
  }
}

static void arrowOutlined(int cx, int cy, int size, int dir, uint16_t c) {
  ui_arrow(cx + 1, cy + 1, size + 1, dir, UI_SHADOW);
  ui_arrow(cx, cy, size, dir, c);
}

const char* ui_hintNav() {
#if !PLATFORM_ESP32
  return "ARROWS MOVE   ENTER OK   ESC BACK";
#elif BOARD == BOARD_DEVKIT
  return "LEFT / RIGHT MOVE   BOTH BUTTONS OK";
#else
  return "STICK MOVE   B OK   SELECT BACK";
#endif
}

const char* ui_hintConfirm() {
#if !PLATFORM_ESP32
  return "PRESS ENTER";
#elif BOARD == BOARD_DEVKIT
  return "PRESS BOTH BUTTONS";
#else
  return "PRESS START";
#endif
}

// How to drive, two short lines (shown on the grid)
static const char* hintDrive(int line) {
#if !PLATFORM_ESP32
  return line ? "SPACE BRAKE  ENTER PAUSE" : "ARROWS STEER  X TURBO";
#elif BOARD == BOARD_DEVKIT
  return line ? "HOLD BOTH TO PAUSE" : "BUTTONS STEER";
#else
  return line ? "A BRAKE  START PAUSE" : "STICK STEER  B TURBO";
#endif
}

void ui_dim(int a) {
  if (a > 0) gfx_fillRectMode(0, 0, SCR_W, SCR_H, COL_BLACK, GFX_BLEND, a);
}

void ui_fade(float t, float dur) {
  if (!(dur > 0) || t >= dur) return;
  int a = (int)((1.0f - clamp01(t / dur)) * 32.0f);
  ui_dim(a);
}

void ui_fadeOut(float t, float dur) {
  if (!(dur > 0)) return;
  ui_dim(alphaOf(clamp01(t / dur)));
}

// Footer line with the controls of this board
static void footer(const char* s, float t) {
  (void)t;
  gfx_fillRect(0, SCR_H - 13, SCR_W, 13, UI_RGB(8, 10, 28));   // solid: hides the frozen HUD under it
  gfx_hspan(SCR_H - 13, 0, SCR_W, UI_EDGE);
  ui_label(SCR_CX, SCR_H - 10, s, UI_DIM, 1, UI_CENTER);
}

// A prompt that breathes (alpha 14..32)
static void prompt(int y, const char* s, float t, int scale = 1, uint16_t c = UI_TEXT) {
  int a = 23 + (int)(9.0f * sinf(t * 5.0f));
  labelA(SCR_CX, y, s, c, scale, UI_CENTER, a);
}

// A small rounded tag ("NEW RECORD")
static void tag(int cx, int y, const char* s, uint16_t bg, uint16_t fg) {
  int w = ui_textWidth(s, 1) + 10;
  int x = cx - w / 2;
  gfx_fillRect(x + 1, y, w - 2, 11, bg);
  gfx_vspan(x, y + 1, y + 10, bg);
  gfx_vspan(x + w - 1, y + 1, y + 10, bg);
  UiTextStyle st = ui_style(1, fg);
  st.outlinePx = 0;
  ui_text((float)cx, (float)(y + 2), s, st, UI_CENTER);
}

// Selection bar that glides between rows (one per menu, keyed by `key`)
static float glideTo(const void* key, float target, float t) {
  static const void* sKey = nullptr;
  static float sY = 0, sLastT = 0;
  float dt = t - sLastT;
  sLastT = t;
  if (key != sKey || dt < 0 || dt > 0.5f) { sKey = key; sY = target; return sY; }
  sY += (target - sY) * clamp01(dt * 18.0f);
  if (fabsf(target - sY) < 0.3f) sY = target;
  return sY;
}

// ═══════════════════════════════════════════════════════════════
//  3. SHOWROOM BACKDROP
// ═══════════════════════════════════════════════════════════════
static void neonFloor(float t) {
  const int hz = SCR_CY;
  const uint16_t top = lerpCol(pal.skylineNear, COL_BLACK, 0.45f);
  const uint16_t bot = UI_RGB(5, 5, 12);
  for (int y = hz; y < SCR_H; y++) {
    float k = (float)(y - hz) / (float)(SCR_H - hz);
    gfx_ditherRow(y, 0, SCR_W, top, bot, sqrtf(k));
  }
  const uint16_t gc = lerpCol(pal.sunGlow, UI_ACCENT, 0.55f);
  // horizontal lines rushing towards the viewer (perspective spacing)
  const float ph = fmodf(t * 0.8f, 1.0f);
  for (int k = 0; k < 18; k++) {
    float z = 1.0f + ((float)k - ph) * 0.42f;
    if (z < 1.0f) continue;
    int y = hz + (int)((SCR_H - hz) / z);
    if (y <= hz) continue;
    gfx_hspanMode(y, 0, SCR_W, gc, GFX_ADD, iclamp((int)(3.0f + 13.0f / z), 0, 32));
  }
  // lines converging to the vanishing point
  for (int k = -8; k <= 8; k++) {
    float xb = SCR_CX + k * 46.0f;
    gfx_fillTri(xb - 1.3f, (float)SCR_H, xb + 1.3f, (float)SCR_H, (float)SCR_CX, (float)hz, gc, GFX_ADD, 8);
  }
  gfx_hspanMode(hz, 0, SCR_W, gc, GFX_ADD, 22);
  gfx_hspanMode(hz + 1, 0, SCR_W, gc, GFX_ADD, 10);
}

static void showroomCar(float x, float y, float ppu, float yaw, uint16_t paint) {
  CarRender cr;
  cr.ax = x; cr.ay = y;
  cr.pxPerUnit = ppu;
  cr.yaw = yaw;
  cr.pitch = 0.32f;
  cr.roll = 0;
  cr.camDist = 14.0f;
  cr.paint = paint;
  cr.fog = 0; cr.fogColor = 0;
  cr.lightScale = 1.35f;   // a studio key light: the navy paint reads too dark at dusk
  cr.tunnelLight = false;
  cr.lights = true;
  cr.brake = false;
  cr.shadow = true;
  renderCar(cr);
}

static void spotlight(int cx, int cy, float scale) {
  gfx_fillEllipse(cx, cy, (int)(122 * scale), (int)(27 * scale), pal.sunGlow, GFX_ADD, 3);
  gfx_fillEllipse(cx, cy, (int)(94 * scale), (int)(20 * scale), UI_RGB(24, 22, 40), GFX_BLEND, 26);
  gfx_fillEllipse(cx, cy, (int)(90 * scale), (int)(18 * scale), UI_RGB(52, 52, 74), GFX_BLEND, 28);
  gfx_fillEllipse(cx, cy - 1, (int)(70 * scale), (int)(12 * scale), UI_RGB(74, 74, 100), GFX_BLEND, 14);
}

static void showroom(float t, float carX, float carY, float ppu, uint16_t paint) {
  drawSky(t * 12.0f, t);
  neonFloor(t);
  spotlight((int)carX, (int)carY - 2, ppu / 17.0f);
  showroomCar(carX, carY, ppu, t * 1.1f + 2.4f, paint);
}

void ui_drawMenuBackdrop(float t) {
  showroom(t, 262.0f, 192.0f, 17.0f, PLAYER_PAINT);
}

// ═══════════════════════════════════════════════════════════════
//  4. TITLE
// ═══════════════════════════════════════════════════════════════
static void logo(int y, float t, float intro) {
  // dark band with accent rules and speed streaks
  shadeRect(0, y - 8, SCR_W, 56, 16);
  gfx_hspan(y - 8, 0, SCR_W, UI_ACCENT);
  gfx_hspanMode(y - 7, 0, SCR_W, UI_ACCENT, GFX_BLEND, 12);
  gfx_hspan(y + 47, 0, SCR_W, UI_ACCENT);
  gfx_hspanMode(y + 46, 0, SCR_W, UI_ACCENT, GFX_BLEND, 12);
  const float in = easeOut(intro / 0.7f);
  const int off = (int)((1.0f - in) * -SCR_W);
  for (int i = 0; i < 6; i++) {
    int ly = y + 6 + i * 6;
    int len = 30 + (int)(26.0f * (0.5f + 0.5f * sinf(t * 3.0f + i * 1.7f)));
    gfx_hspanMode(ly, off + 2, off + 2 + len, UI_ACCENT2, GFX_ADD, 10 + (i & 1) * 6);
  }
  UiTextStyle a = ui_styleChrome(5.0f);
  a.slant = 0.22f;
  UiTextStyle b = styleHot(5.0f);
  b.slant = 0.22f;
  const int wA = ui_textWidth("ARCADE", 5.0f), wB = ui_textWidth("3D", 5.0f);
  const int gap = 16;
  int x0 = SCR_CX - (wA + gap + wB) / 2 - 3 + off;
  ui_text((float)x0, (float)y, "ARCADE", a, UI_LEFT);
  ui_text((float)(x0 + wA + gap), (float)y, "3D", b, UI_LEFT);
}

static void circuitNames(int y, int featured, float t) {
  // "NEON CITY <> SUNSET COAST <> ALPINE PASS", the featured one lit
  int w = 0;
  for (int i = 0; i < NUM_CIRCUITS; i++) w += ui_textWidth(CIRCUITS[i].name, 1) + (i ? 16 : 0);
  int x = SCR_CX - w / 2;
  for (int i = 0; i < NUM_CIRCUITS; i++) {
    if (i) {
      gfx_fillQuad((float)x + 5, (float)y + 3.5f, (float)x + 8, (float)y + 0.5f, (float)x + 11, (float)y + 3.5f,
                   (float)x + 8, (float)y + 6.5f, UI_ACCENT);
      x += 16;
    }
    bool on = i == featured;
    uint16_t c = on ? lerpCol(UI_GOLD, COL_WHITE, 0.25f + 0.25f * sinf(t * 6.0f)) : UI_DIM;
    ui_label(x, y, CIRCUITS[i].name, c, 1, UI_LEFT);
    if (on) gfx_hspan(y + 9, x, x + ui_textWidth(CIRCUITS[i].name, 1), UI_GOLD);
    x += ui_textWidth(CIRCUITS[i].name, 1);
  }
}

void ui_drawTitleEx(float t, int featured, float ft) {
  showroom(t, (float)SCR_CX, 190.0f, 17.0f, PLAYER_PAINT);
  // dip to black around the change of featured circuit (theme swap)
  const float D = 0.3f;
  if (ft < D) ui_dim(alphaOf(1.0f - ft / D));
  else if (ft > UI_TITLE_FEATURE_TIME - D) ui_dim(alphaOf((ft - (UI_TITLE_FEATURE_TIME - D)) / D));
  logo(18, t, game_state() == GS_TITLE ? game_stateTime() : 10.0f);
  circuitNames(78, featured, t);
  prompt(SCR_H - 26, ui_hintConfirm(), t, 2, UI_TEXT);
}

void ui_drawTitle(float t) {
  ui_drawTitleEx(t, currentCircuit, 1.0f);
}

// ═══════════════════════════════════════════════════════════════
//  5. MENUS
// ═══════════════════════════════════════════════════════════════
void ui_drawMenu(const char* title, const char* const* items, int count, int sel, float t) {
  const int rowH = 24, w = 220;
  const int h = 50 + count * rowH + 8;
  const int x = (SCR_W - w) / 2, y = (SCR_H - h) / 2 - 6;
  ui_panel(x, y, w, h, UI_ACCENT);
  ui_text((float)SCR_CX, (float)(y + 12), title, ui_styleChrome(3.0f), UI_CENTER);
  const int y0 = y + 50;
  float by = glideTo(items, (float)(y0 + sel * rowH - 5), t);
  ui_selBar(x + 10, (int)by, w - 20, 22, t);
  for (int i = 0; i < count; i++) {
    bool s = i == sel;
    ui_label(SCR_CX, y0 + i * rowH, items[i], s ? COL_WHITE : UI_DIM, 2, UI_CENTER);
  }
  footer(ui_hintNav(), t);
}

static const char* const DIFF_NAMES[3] = { "EASY", "NORMAL", "HARD" };
static const uint16_t DIFF_COLS[3] = { UI_GOOD, UI_GOLD, UI_BAD };

void ui_drawMainMenu(int sel, int diff, float t) {
  static const char* const ITEMS[4] = { "GRAND PRIX", "QUICK RACE", "DIFFICULTY", "BACK" };
  static const char* const HELP1[4] = { "THREE RACES IN A ROW", "ONE RACE ON THE CIRCUIT", "HOW FAST AND BOLD", "BACK TO THE" };
  static const char* const HELP2[4] = { "POINTS 10-7-5-3-2-1", "OF YOUR CHOICE", "THE RIVALS DRIVE", "TITLE SCREEN" };
  diff = iclamp(diff, 0, 2);
  const int x = 8, y = 12, w = 190, h = 200;
  ui_panel(x, y, w, h, UI_ACCENT);
  UiTextStyle hs = ui_styleChrome(2.0f);
  ui_text((float)(x + 12), (float)(y + 11), "MAIN MENU", hs, UI_LEFT);
  gfx_hspanMode(y + 31, x + 10, x + w - 10, UI_EDGE, GFX_BLEND, 24);

  const int rowH = 30, y0 = y + 44;
  float by = glideTo(ITEMS, (float)(y0 + sel * rowH - 6), t);
  ui_selBar(x + 6, (int)by, w - 12, 25, t);
  for (int i = 0; i < 4; i++) {
    bool s = i == sel;
    int iy = y0 + i * rowH;
    if (s) {
      int nudge = (int)(2.0f * sinf(t * 8.0f));
      arrowOutlined(x + 16 + nudge, iy + 6, 4, 3, COL_WHITE);
    }
    ui_label(x + 26, iy, ITEMS[i], s ? COL_WHITE : UI_DIM, 2, UI_LEFT);
    if (i == 2) {   // value of the difficulty: three pips and the name
      for (int k = 0; k < 3; k++)
        gfx_fillRect(x + 26 + k * 8, iy + 18, 6, 5, k <= diff ? DIFF_COLS[diff] : UI_RGB(50, 54, 80));
      const char* v = DIFF_NAMES[diff];
      const int vx = x + 64;
      ui_label(vx, iy + 17, v, DIFF_COLS[diff], 1, UI_LEFT);
      if (s) {
        int vw = ui_textWidth(v, 1);
        arrowOutlined(vx - 7, iy + 20, 3, 2, COL_WHITE);
        arrowOutlined(vx + vw + 6, iy + 20, 3, 3, COL_WHITE);
      }
    }
  }
  // help for the selected item
  const int hy = y + h - 30;
  gfx_hspanMode(hy - 6, x + 10, x + w - 10, UI_EDGE, GFX_BLEND, 24);
  ui_label(x + 12, hy, HELP1[iclamp(sel, 0, 3)], UI_ACCENT2, 1, UI_LEFT);
  ui_label(x + 12, hy + 11, HELP2[iclamp(sel, 0, 3)], UI_ACCENT2, 1, UI_LEFT);

  // save statistics, bottom right
  char buf[32];
  snprintf(buf, sizeof(buf), "GP WINS %u", (unsigned)(saveData.gpWins[0] + saveData.gpWins[1] + saveData.gpWins[2]));
  ui_label(SCR_W - 8, 14, buf, UI_GOLD, 1, UI_RIGHT);
  snprintf(buf, sizeof(buf), "RACES %u", (unsigned)saveData.races);
  ui_label(SCR_W - 8, 25, buf, UI_DIM, 1, UI_RIGHT);
  footer(ui_hintNav(), t);
}

// ═══════════════════════════════════════════════════════════════
//  6. MINIMAP
//  Small maps (the HUD) are rasterised once per circuit into two
//  64x64 bit masks (road, road grown by one pixel) and drawn as spans:
//  thick, outlined and cheaper than 256 line calls. Big maps (the
//  select screen) are drawn with quads.
// ═══════════════════════════════════════════════════════════════
#define MM_MAX 64
static uint64_t sMMRoad[MM_MAX], sMMEdge[MM_MAX];
static uint32_t sMMKey = 0;

static uint32_t minimapKey(int size) {
  uint32_t h = 2166136261u ^ (uint32_t)size;
  for (int i = 0; i < MINIMAP_PTS; i++) {
    h = (h ^ minimapX[i]) * 16777619u;
    h = (h ^ minimapY[i]) * 16777619u;
  }
  return h ^ ((uint32_t)segCount << 7);
}

static inline float mmCoord(uint8_t v, int size) { return 1.5f + v * (size - 4.0f) / 255.0f; }

static void mmSet(int x, int y) {
  if (x < 0 || y < 0 || x >= MM_MAX || y >= MM_MAX) return;
  sMMRoad[y] |= 1ull << x;
}

static void mmBuild(int size) {
  memset(sMMRoad, 0, sizeof(sMMRoad));
  memset(sMMEdge, 0, sizeof(sMMEdge));
  for (int i = 0; i < MINIMAP_PTS; i++) {
    int j = (i + 1) % MINIMAP_PTS;
    float ax = mmCoord(minimapX[i], size), ay = mmCoord(minimapY[i], size);
    float bx = mmCoord(minimapX[j], size), by = mmCoord(minimapY[j], size);
    float len = fmaxF(fabsf(bx - ax), fabsf(by - ay));
    int steps = (int)(len * 2.0f) + 1;
    for (int k = 0; k <= steps; k++) {
      float f = (float)k / steps;
      int px = (int)floorf(ax + (bx - ax) * f), py = (int)floorf(ay + (by - ay) * f);
      mmSet(px, py); mmSet(px + 1, py); mmSet(px, py + 1); mmSet(px + 1, py + 1);
    }
  }
  for (int y = 0; y < MM_MAX; y++) {
    uint64_t m = sMMRoad[y];
    if (y > 0) m |= sMMRoad[y - 1];
    if (y < MM_MAX - 1) m |= sMMRoad[y + 1];
    sMMEdge[y] = m | (m << 1) | (m >> 1);
  }
}

static void mmDrawMask(const uint64_t* m, int n, int x, int y, uint16_t c) {
  for (int r = 0; r < n; r++) {
    const int py = y + r;
    if (py < gfx_cy0 || py >= gfx_cy1) continue;
    uint16_t* row = gfx_fb + py * SCR_W;
    uint64_t b = m[r];
    while (b) {
      int s = __builtin_ctzll(b);
      uint64_t rest = ~(b >> s);
      int len = rest ? __builtin_ctzll(rest) : 64 - s;
      runFill(row, x + s, x + s + len, c, 32);
      if (s + len >= 64) break;
      b &= ~(((1ull << len) - 1) << s);
    }
  }
}

static void startFlagAt(int x, int y) {
  for (int yy = 0; yy < 4; yy++)
    for (int xx = 0; xx < 4; xx++)
      gfx_pixel(x + xx, y + yy, ((xx >> 1) + (yy >> 1)) & 1 ? COL_BLACK : COL_WHITE);
}

static void racerDots(int x, int y, int size, float inset, float t) {
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < NUM_RACERS; i++) {
      bool me = i == PLAYER_ID;
      if ((pass == 1) != me) continue;
      float u, v;
      minimapAt(racers[i].z, u, v);
      int dx = x + (int)(inset + u * (size - 2.0f * inset - 1.0f) + 0.5f);
      int dy = y + (int)(inset + v * (size - 2.0f * inset - 1.0f) + 0.5f);
      if (me) {
        int r = 3 + (((int)(t * 4.0f)) & 1);
        gfx_fillRect(dx - r, dy - r, 2 * r + 1, 2 * r + 1, UI_SHADOW);
        gfx_fillRect(dx - r + 1, dy - r + 1, 2 * r - 1, 2 * r - 1, COL_WHITE);
        gfx_fillRect(dx - 1, dy - 1, 3, 3, UI_ACCENT);
      } else {
        gfx_fillRect(dx - 2, dy - 2, 5, 5, UI_SHADOW);
        gfx_fillRect(dx - 1, dy - 1, 3, 3, paintOf(i));
      }
    }
  }
}

// Big outline with quads (select screen, champion): cost does not matter
static void minimapBig(int x, int y, int size, float t, bool runner) {
  const float inset = 4.0f;
  const float k = (size - 2.0f * inset) / 255.0f;
  for (int pass = 0; pass < 2; pass++) {
    float hw = pass ? 1.2f : 2.6f;
    uint16_t c = pass ? UI_RGB(236, 238, 248) : UI_SHADOW;
    for (int i = 0; i < MINIMAP_PTS; i++) {
      int j = (i + 1) % MINIMAP_PTS;
      float ax = x + inset + minimapX[i] * k, ay = y + inset + minimapY[i] * k;
      float bx = x + inset + minimapX[j] * k, by = y + inset + minimapY[j] * k;
      float dx = bx - ax, dy = by - ay;
      float l = sqrtf(dx * dx + dy * dy);
      if (l < 1e-3f) continue;
      float nx = -dy / l * hw, ny = dx / l * hw;
      float ex = dx / l * hw * 0.5f, ey = dy / l * hw * 0.5f;   // overlap the joints
      gfx_fillQuad(ax + nx - ex, ay + ny - ey, bx + nx + ex, by + ny + ey,
                   bx - nx + ex, by - ny + ey, ax - nx - ex, ay - ny - ey, c);
    }
  }
  startFlagAt(x + (int)(inset + minimapX[0] * k) - 2, y + (int)(inset + minimapY[0] * k) - 2);
  if (runner) {
    // a comet running the lap, to show the direction
    for (int s = 5; s >= 0; s--) {
      float f = fmodf(t * 0.07f - s * 0.008f + 10.0f, 1.0f);
      float u, v;
      minimapAt(f * trackLength, u, v);
      int px = x + (int)(inset + u * (size - 2.0f * inset)), py = y + (int)(inset + v * (size - 2.0f * inset));
      uint16_t c = s == 0 ? UI_ACCENT : lerpCol(UI_ACCENT, UI_INK, s / 6.0f);
      int r = s == 0 ? 2 : 1;
      gfx_fillRect(px - r, py - r, 2 * r + 1, 2 * r + 1, c);
    }
  }
}

void ui_drawMinimap(int x, int y, int size, bool dots) {
  if (size < 8 || segCount <= 0) return;
  if (size > MM_MAX) {
    minimapBig(x, y, size, 0, false);
    if (dots) racerDots(x, y, size, 4.0f, 0);
    return;
  }
  uint32_t key = minimapKey(size);
  if (key != sMMKey) { mmBuild(size); sMMKey = key; }
  mmDrawMask(sMMEdge, size, x, y, UI_SHADOW);
  mmDrawMask(sMMRoad, size, x, y, UI_RGB(226, 230, 242));
  startFlagAt(x + (int)mmCoord(minimapX[0], size) - 1, y + (int)mmCoord(minimapY[0], size) - 1);
  if (dots) racerDots(x, y, size, 2.0f, (float)raceTime);
}

// ═══════════════════════════════════════════════════════════════
//  7. CIRCUIT SELECT (carousel over a flyover of the circuit)
// ═══════════════════════════════════════════════════════════════
static float kmOf(float len) {
  // the speedometer shows speed * 300 / maxSpeed km/h
  return (maxSpeed > 0) ? len * 300.0f / (3.6f * maxSpeed) / 1000.0f : 0.0f;
}

void ui_drawCircuitSelectEx(int sel, float t, float selT) {
  const bool back = sel < 0 || sel >= NUM_CIRCUITS;
  // header
  shadeRect(0, 0, SCR_W, 30, 20);
  gfx_hspan(30, 0, SCR_W, UI_ACCENT);
  ui_text(10.0f, 8.0f, "SELECT CIRCUIT", ui_styleChrome(2.0f), UI_LEFT);
  // pips: circuits + back
  for (int i = 0; i <= NUM_CIRCUITS; i++) {
    int px = SCR_W - 14 - (NUM_CIRCUITS - i) * 13;
    bool on = i == sel || (back && i == NUM_CIRCUITS);
    gfx_fillRect(px - 1, 11, 10, 8, UI_SHADOW);
    gfx_fillRect(px, 12, 8, 6, on ? UI_ACCENT : UI_RGB(60, 64, 96));
  }

  // minimap card, top right
  if (!back && currentCircuit == sel) {
    const int mx = 208, my = 38, ms = 104;
    ui_panel(mx - 4, my - 4, ms + 8, ms + 8, 0, 18);
    minimapBig(mx, my, ms, t, true);
  }

  // bottom panel with the name and the facts
  const int py = 150, ph = 74;
  ui_panel(6, py, SCR_W - 12, ph, UI_ACCENT, 21);
  float in = easeOut(selT / 0.25f);
  int slide = (int)((1.0f - in) * 24.0f);
  const char* name = back ? "BACK" : CIRCUITS[sel].name;
  UiTextStyle ns = ui_styleChrome(3.0f);
  ns.alpha = (uint8_t)alphaOf(in);
  ui_text((float)(SCR_CX + slide), (float)(py + 9), name, ns, UI_CENTER);
  int nudge = (int)(2.5f * sinf(t * 7.0f));
  arrowOutlined(20 - nudge, py + 19, 6, 2, UI_ACCENT);
  arrowOutlined(SCR_W - 20 + nudge, py + 19, 6, 3, UI_ACCENT);

  char buf[48], tb[24];
  if (back) {
    ui_label(SCR_CX, py + 38, "RETURN TO THE MAIN MENU", UI_ACCENT2, 1, UI_CENTER);
  } else {
    const CircuitInfo& c = CIRCUITS[sel];
    ui_label(SCR_CX, py + 36, c.tagline, UI_ACCENT2, 1, UI_CENTER);
    float km = currentCircuit == sel ? kmOf(trackLength) : 0.0f;
    if (km > 0) snprintf(buf, sizeof(buf), "%d LAPS   %.1f KM", c.laps, km);
    else snprintf(buf, sizeof(buf), "%d LAPS", c.laps);
    ui_label(SCR_CX, py + 48, buf, UI_TEXT, 1, UI_CENTER);
    float bl = saveData.bestLap[sel], br = saveData.bestRace[sel];
    if (bl > 0) ui_fmtTime(tb, sizeof(tb), bl); else snprintf(tb, sizeof(tb), "--:--.--");
    snprintf(buf, sizeof(buf), "BEST LAP %s", tb);
    ui_label(SCR_CX - 8, py + 60, buf, UI_GOLD, 1, UI_RIGHT);
    if (br > 0) ui_fmtTime(tb, sizeof(tb), br); else snprintf(tb, sizeof(tb), "--:--.--");
    snprintf(buf, sizeof(buf), "BEST RACE %s", tb);
    ui_label(SCR_CX + 8, py + 60, buf, UI_GOLD, 1, UI_LEFT);
  }
  footer(ui_hintNav(), t);
}

void ui_drawCircuitSelect(int sel, float t) {
  ui_drawCircuitSelectEx(sel, t, 1.0f);
}

// ═══════════════════════════════════════════════════════════════
//  8. COUNTDOWN AND GO
//  Gantry with five lights: they switch on in a ripple while the
//  grid gets ready, then go out one by one during 3-2-1; the last one
//  goes out on GO, when all five turn green.
// ═══════════════════════════════════════════════════════════════
#define GANTRY_Y 58   // below the HUD's top panels (they end at y = 52)

static void gantry(int y, int litMask, bool green) {
  const int cx = SCR_CX, w = 146, h = 30;
  const int x = cx - w / 2;
  // one central support between the HUD panels
  gfx_fillRect(cx - 2, 0, 4, y, UI_RGB(30, 30, 36));
  gfx_vspan(cx - 1, 0, y, UI_RGB(70, 70, 82));
  // housing
  gfx_fillRect(x - 1, y - 1, w + 2, h + 2, UI_SHADOW);
  gfx_fillRect(x, y, w, h, UI_RGB(26, 26, 32));
  gfx_hspan(y, x, x + w, UI_RGB(70, 70, 82));
  gfx_hspan(y + h - 1, x, x + w, UI_RGB(14, 14, 18));
  for (int i = 0; i < 5; i++) {
    int lx = x + 17 + i * 28, ly = y + h / 2;
    bool on = (litMask >> i) & 1;
    uint16_t c = green ? UI_RGB(60, 255, 110) : (on ? UI_RGB(255, 36, 24) : UI_RGB(62, 14, 12));
    gfx_fillCircle(lx, ly, 11, UI_RGB(8, 8, 10));
    gfx_fillCircle(lx, ly, 9, c);
    if (green || on) {
      gfx_fillCircle(lx - 2, ly - 3, 3, lerpCol(c, COL_WHITE, 0.6f));
      gfx_glow(lx, ly, 22, c, 16);
    }
  }
}

static int countdownLights(float t) {
  int mask = 0;
  for (int i = 0; i < 5; i++) {
    bool on = t >= 0.5f + i * 0.1f && t < 1.6f + i * 0.6f;
    if (on) mask |= 1 << i;
  }
  return mask;
}

int ui_countdownLightMask(float t, bool* green) {
  const bool g = t >= UI_COUNTDOWN_GO && t < UI_COUNTDOWN_GO + UI_GO_GREEN;
  if (green) *green = g;
  return g ? 0x1F : countdownLights(t);
}

// Big 3-2-1 digit popping in each second (t = countdown time)
static void countdownDigit(float t) {
  if (t < 1.0f || t >= UI_COUNTDOWN_GO) return;
  int n = 3 - (int)(t - 1.0f);
  float u = (t - 1.0f) - floorf(t - 1.0f);
  char d[2] = { (char)('0' + iclamp(n, 1, 3)), 0 };
  float sc = 8.0f + 4.0f * (1.0f - easeOut(u / 0.16f));
  UiTextStyle s = ui_styleGold(sc);
  s.outlinePx = 3;
  s.shadowDx = 3; s.shadowDy = 3;
  s.alpha = (uint8_t)alphaOf(u < 0.7f ? 1.0f : (1.0f - u) / 0.3f);
  ui_text((float)SCR_CX, 120.0f - 3.5f * sc, d, s, UI_CENTER);
}

void ui_drawCountdownLights(float t) {
  float drop = easeOutBack(t / 0.45f);
  gantry((int)(-34.0f + (GANTRY_Y + 34.0f) * drop), countdownLights(t), false);
  countdownDigit(t);
}

void ui_drawCountdownEx(float t, bool gp, int gpRace, int gpRaces) {
  float drop = easeOutBack(t / 0.45f);
  int gy = (int)(-34.0f + (GANTRY_Y + 34.0f) * drop);
  gantry(gy, countdownLights(t), false);

  // how to drive, while the grid waits (between the minimap and the dial)
  if (t > 1.0f && t < UI_COUNTDOWN_GO - 0.1f) {
    int a = alphaOf(fminF((t - 1.0f) / 0.3f, (UI_COUNTDOWN_GO - 0.1f - t) / 0.3f));
    int w = imax(ui_textWidth(hintDrive(0), 1), ui_textWidth(hintDrive(1), 1)) + 12;
    if (a > 12) shadeRect(SCR_CX - w / 2, 150, w, 24, a > 24 ? 20 : 16);
    labelA(SCR_CX, 153, hintDrive(0), UI_TEXT, 1, UI_CENTER, a);
    labelA(SCR_CX, 164, hintDrive(1), UI_TEXT, 1, UI_CENTER, a);
  }

  if (t < 1.0f) {
    int a = alphaOf(fminF(t / 0.2f, (1.0f - t) / 0.15f));
    char buf[40];
    if (gp) snprintf(buf, sizeof(buf), "GRAND PRIX  RACE %d/%d", gpRace + 1, gpRaces);
    else snprintf(buf, sizeof(buf), "QUICK RACE  %d LAPS", curCircuit().laps);
    UiTextStyle s = ui_styleChrome(3.0f);
    s.alpha = (uint8_t)a;
    ui_text((float)SCR_CX, 98.0f, "GET READY", s, UI_CENTER);
    UiTextStyle n = ui_styleGold(2.0f);
    n.alpha = (uint8_t)a;
    ui_text((float)SCR_CX, 124.0f, curCircuit().name, n, UI_CENTER);
    labelA(SCR_CX, 144, buf, UI_ACCENT2, 1, UI_CENTER, a);
  } else {
    countdownDigit(t);
  }
}

void ui_drawCountdown(float t) {
  ui_drawCountdownEx(t, game_isGP(), game_gpRace(), NUM_CIRCUITS);
}

void ui_drawGo(float since) {
  if (since < 0 || since > 1.3f) return;
  float up = clamp01((since - 0.8f) / 0.4f);
  int gy = (int)(GANTRY_Y - (GANTRY_Y + 34.0f) * up * up);
  gantry(gy, 0x1F, true);
  float sc = 9.0f + 4.0f * easeOut(since / 0.9f);
  UiTextStyle s = styleGreen(sc);
  s.outlinePx = 3;
  s.shadowDx = 4; s.shadowDy = 4;
  s.alpha = (uint8_t)alphaOf(since < 0.6f ? 1.0f : (1.0f - since) / 0.4f);
  ui_text((float)SCR_CX, 120.0f - 3.5f * sc, "GO!", s, UI_CENTER);
}

// ═══════════════════════════════════════════════════════════════
//  9. RACE BANNERS, FINISH, CRASH, DEMO
// ═══════════════════════════════════════════════════════════════
#define BANNER_TIME 2.4f

void ui_drawBannerEx(const char* text, const char* sub, const char* tg, uint16_t color, float t) {
  if (t < 0 || t > BANNER_TIME) return;
  float in = easeOut(t / 0.22f);
  float out = clamp01((t - (BANNER_TIME - 0.3f)) / 0.3f);
  int off = (int)((1.0f - in) * SCR_W - out * out * SCR_W);
  // strip under the HUD's gap line (y 56..69), opening and closing vertically
  const int y = 74, h = sub ? 40 : 30;
  float open = fminF(easeOut(t / 0.15f), 1.0f - out);
  int hh = (int)(h * open + 0.5f);
  if (hh < 2) return;
  int y0 = y + (h - hh) / 2;
  shadeRect(0, y0, SCR_W, hh, 18);
  gfx_hspan(y0, 0, SCR_W, color);
  gfx_hspan(y0 + hh - 1, 0, SCR_W, color);
  int sy0 = gfx_cy0, sy1 = gfx_cy1;
  gfx_setClip(gfx_cx0, imax(y0, sy0), gfx_cx1, imin(y0 + hh, sy1));
  UiTextStyle s = ui_styleGrad(3.0f, lerpCol(color, COL_WHITE, 0.55f), color);
  ui_text((float)(SCR_CX + off), (float)(y + 5), text, s, UI_CENTER);
  if (sub) ui_label(SCR_CX - off, y + 28, sub, UI_TEXT, 1, UI_CENTER);
  gfx_setClip(gfx_cx0, sy0, gfx_cx1, sy1);
  if (tg && out <= 0 && (((int)(t * 6.0f)) & 1) == 0) tag(SCR_CX - off, y + h + 3, tg, UI_GOLD, UI_SHADOW);
}

void ui_drawBanner(const char* text, uint16_t color, float t) {
  ui_drawBannerEx(text, nullptr, nullptr, color, t);
}

static void chequerStrip(int y, int h, float scroll) {
  const int sq = h / 2;
  if (sq <= 0) return;
  int off = ((int)scroll) % (2 * sq);
  for (int row = 0; row < 2; row++) {
    for (int x = -2 * sq + off; x < SCR_W; x += sq) {
      int k = ((x - off) / sq + row) & 1;
      gfx_fillRect(x, y + row * sq, sq, sq, k ? UI_RGB(20, 20, 24) : UI_RGB(240, 240, 244));
    }
  }
}

void ui_drawFinishEx(float t, int place, float raceTimeSec, int recordFlags) {
  float in = easeOut(t / 0.35f);
  const int y = 56;   // between the HUD's top panels and the minimap / dial
  int slide = (int)((1.0f - in) * SCR_W);
  shadeRect(0, y + 8, SCR_W, 84, 20);
  chequerStrip(y, 8, t * 40.0f + slide);
  chequerStrip(y + 92, 8, 1000.0f - t * 40.0f - slide);
  float zoom = 5.0f + 3.0f * (1.0f - easeOut(t / 0.3f));
  UiTextStyle f = ui_styleChrome(zoom);
  ui_text((float)SCR_CX, (float)(y + 30) - 3.5f * zoom, "FINISH", f, UI_CENTER);
  if (t > 0.35f) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%d%s PLACE", place, placeSuffix(place));
    UiTextStyle p = place <= 3 ? ui_styleGrad(3.0f, lerpCol(placeColor(place), COL_WHITE, 0.5f), placeColor(place))
                               : ui_styleGrad(3.0f, COL_WHITE, UI_DIM);
    p.alpha = (uint8_t)alphaOf((t - 0.35f) / 0.2f);
    ui_text((float)SCR_CX, (float)(y + 52), buf, p, UI_CENTER);
    char tb[16];
    ui_fmtTime(tb, sizeof(tb), raceTimeSec);
    snprintf(buf, sizeof(buf), "TIME %s", tb);
    ui_label(SCR_CX, y + 79, buf, UI_TEXT, 1, UI_CENTER);
  }
  if (t > 0.8f && recordFlags && (((int)(t * 5.0f)) & 1) == 0)
    tag(SCR_CX, y + 104, (recordFlags & 2) ? "NEW RACE RECORD" : "NEW LAP RECORD", UI_GOLD, UI_SHADOW);
}

void ui_drawFinish(float t, int place) {
  ui_drawFinishEx(t, place, racers[PLAYER_ID].finishTime, 0);
}

void ui_drawCrash(float since) {
  if (since < 0.3f)   // impact flash
    gfx_fillRectMode(0, 0, SCR_W, SCR_H, COL_WHITE, GFX_BLEND, (int)((1.0f - since / 0.3f) * 18.0f));
  const int y = SCR_CY - 30, h = 44;
  float in = easeOut(since / 0.2f);
  int hh = (int)(h * in);
  int y0 = y + (h - hh) / 2;
  shadeRect(0, y0, SCR_W, hh, 20);
  // hazard stripes on both edges of the strip
  int s0 = gfx_cy0, s1 = gfx_cy1;
  for (int edge = 0; edge < 2; edge++) {
    int ey = edge ? y0 + hh - 4 : y0;
    gfx_setClip(0, imax(ey, s0), SCR_W, imin(ey + 4, s1));
    gfx_fillRect(0, ey, SCR_W, 4, UI_GOLD);
    for (int x = -8 + ((int)(since * 30.0f) % 12); x < SCR_W; x += 12)
      gfx_fillQuad((float)x, (float)ey + 4, (float)x + 4, (float)ey, (float)x + 10, (float)ey, (float)x + 6,
                   (float)ey + 4, UI_SHADOW);
  }
  gfx_setClip(0, s0, SCR_W, s1);
  if (in < 0.6f) return;
  int jx = since < 0.5f ? (int)(sinf(since * 90.0f) * 4.0f * (1.0f - since / 0.5f)) : 0;
  UiTextStyle s = styleHot(4.0f);
  ui_text((float)(SCR_CX + jx), (float)(y + 8), "CRASH!", s, UI_CENTER);
  // recovery bar
  float k = clamp01(since / CRASH_TIME);
  const int bw = 120, bx = SCR_CX - bw / 2, byy = y + h + 6;
  shadeRect(bx - 2, byy - 2, bw + 4, 8, 24);
  gfx_fillRect(bx, byy, (int)(bw * k), 4, UI_GOLD);
  ui_label(SCR_CX, byy + 8, "RECOVERING", UI_DIM, 1, UI_CENTER);
}

void ui_drawDemoBanner(float t) {
  // badge between the HUD's top panels: DEMO + the circuit
  ui_panel(SCR_CX - 44, 2, 88, 36, UI_ACCENT, 22);
  UiTextStyle s = ui_styleChrome(2.0f);
  s.alpha = (uint8_t)(24 + (int)(8.0f * sinf(t * 3.0f)));
  ui_text((float)SCR_CX, 7.0f, "DEMO", s, UI_CENTER);
  ui_label(SCR_CX, 26, curCircuit().name, UI_ACCENT2, 1, UI_CENTER);
  if (t > UI_COUNTDOWN_GO + 0.8f) prompt(100, ui_hintConfirm(), t, 2, UI_GOLD);
}

// ═══════════════════════════════════════════════════════════════
//  10. PAUSE
// ═══════════════════════════════════════════════════════════════
void ui_drawPauseEx(int sel, bool gp, float t) {
  static const char* const ITEMS[3] = { "RESUME", "RESTART", "QUIT" };
  const int w = 200, h = 168, x = (SCR_W - w) / 2, y = 30;
  ui_panel(x, y, w, h, UI_ACCENT, 24);
  ui_text((float)SCR_CX, (float)(y + 12), "PAUSED", ui_styleChrome(3.0f), UI_CENTER);
  // race state
  const Racer& P = racers[PLAYER_ID];
  char buf[80], tb[24];
  ui_fmtTime(tb, sizeof(tb), raceTime);
  snprintf(buf, sizeof(buf), "LAP %d/%d   POS %d/%d   %s", imin(P.lap + 1, curCircuit().laps), curCircuit().laps,
           P.place, NUM_RACERS, tb);
  ui_label(SCR_CX, y + 40, curCircuit().name, UI_ACCENT2, 1, UI_CENTER);
  ui_label(SCR_CX, y + 51, buf, UI_DIM, 1, UI_CENTER);
  const int rowH = 26, y0 = y + 72;
  float by = glideTo(ITEMS, (float)(y0 + sel * rowH - 5), t);
  ui_selBar(x + 12, (int)by, w - 24, 23, t);
  for (int i = 0; i < 3; i++) {
    bool s = i == sel;
    const char* label = (i == 2 && gp) ? "QUIT GP" : ITEMS[i];
    ui_label(SCR_CX, y0 + i * rowH, label, s ? COL_WHITE : UI_DIM, 2, UI_CENTER);
  }
  footer(ui_hintNav(), t);
}

void ui_drawPause(int sel) {
  ui_drawPauseEx(sel, game_isGP(), 0.0f);
}

// ═══════════════════════════════════════════════════════════════
//  11. RESULTS, STANDINGS, CHAMPION
// ═══════════════════════════════════════════════════════════════
// Row slide-in: row p arrives at 0.2 + p * 0.08 s
static int rowSlide(float t, int p) {
  float k = easeOut((t - 0.2f - p * 0.08f) / 0.22f);
  return (int)((1.0f - k) * (SCR_W + 20));
}

static void placeCell(int x, int y, int p) {
  char d[4];
  snprintf(d, sizeof(d), "%d", p);
  UiTextStyle s = ui_styleGrad(2.0f, lerpCol(placeColor(p), COL_WHITE, 0.4f), placeColor(p));
  s.shadowDx = s.shadowDy = 1;
  ui_text((float)x, (float)y, d, s, UI_LEFT);
  ui_label(x + 12, y + 7, placeSuffix(p), placeColor(p), 1, UI_LEFT);
}

void ui_drawResultsEx(const ResultRow* rows, const int* points, int recordFlags, float t) {
  const int x = 6, y = 6, w = SCR_W - 12, h = 216;
  ui_panel(x, y, w, h, UI_ACCENT, 23);
  ui_text((float)(x + 10), (float)(y + 9), "RESULTS", ui_styleChrome(3.0f), UI_LEFT);
  char buf[48], tb[24];
  if (game_isGP()) snprintf(buf, sizeof(buf), "GRAND PRIX  RACE %d/%d", game_gpRace() + 1, NUM_CIRCUITS);
  else snprintf(buf, sizeof(buf), "QUICK RACE");
  ui_label(x + w - 10, y + 10, curCircuit().name, UI_ACCENT2, 1, UI_RIGHT);
  ui_label(x + w - 10, y + 21, buf, UI_DIM, 1, UI_RIGHT);

  const bool gp = points != nullptr;
  // right edges of the columns (the GP adds points at the far right)
  const int cTime = gp ? 204 : 236, cBest = gp ? 254 : 300, cPts = 306;
  const int hy = y + 38;
  ui_label(x + 8, hy, "POS", UI_DIM, 1);
  ui_label(64, hy, "DRIVER", UI_DIM, 1);
  ui_label(cTime, hy, "TIME", UI_DIM, 1, UI_RIGHT);
  ui_label(cBest, hy, gp ? "BEST" : "BEST LAP", UI_DIM, 1, UI_RIGHT);
  if (gp) ui_label(cPts, hy, "PTS", UI_DIM, 1, UI_RIGHT);
  gfx_hspanMode(hy + 10, x + 6, x + w - 6, UI_EDGE, GFX_BLEND, 26);

  // fastest lap of the race
  float fast = 0;
  for (int p = 0; p < NUM_RACERS; p++)
    if (rows[p].bestLap > 0 && (fast <= 0 || rows[p].bestLap < fast)) fast = rows[p].bestLap;

  const int rowH = 23, y0 = hy + 15;
  for (int p = 0; p < NUM_RACERS; p++) {
    const ResultRow& r = rows[p];
    const int ry = y0 + p * rowH;
    const int dx = rowSlide(t, p);
    if (dx >= SCR_W) continue;
    const bool me = r.racer == PLAYER_ID;
    if (me) ui_selBar(x + 4 + dx, ry - 4, w - 8, rowH - 2, t, UI_ACCENT);
    else if (p & 1) gfx_fillRectMode(x + 4 + dx, ry - 4, w - 8, rowH - 2, COL_WHITE, GFX_BLEND, 2);
    placeCell(x + 10 + dx, ry, p + 1);
    ui_carIcon(40 + dx, ry + 2, paintOf(r.racer));
    ui_label(64 + dx, ry, racers[r.racer].name, me ? COL_WHITE : UI_TEXT, 2, UI_LEFT);
    if (p == 0) ui_fmtTime(tb, sizeof(tb), r.time);
    else fmtGap(tb, sizeof(tb), r.time - rows[0].time);
    ui_label(cTime + dx - (r.finished ? 0 : 7), ry + 3, tb, r.finished ? UI_TEXT : UI_DIM, 1, UI_RIGHT);
    if (!r.finished) ui_label(cTime + dx, ry + 3, "*", UI_DIM, 1, UI_RIGHT);
    if (r.bestLap > 0) ui_fmtTime(tb, sizeof(tb), r.bestLap);
    else snprintf(tb, sizeof(tb), "--");
    bool isFast = fast > 0 && r.bestLap == fast;
    ui_label(cBest + dx, ry + 3, tb, isFast ? UI_PURPLE : UI_DIM, 1, UI_RIGHT);
    if (gp) {
      snprintf(buf, sizeof(buf), "+%d", points[r.racer]);
      ui_label(cPts + dx, ry, buf, points[r.racer] >= 10 ? UI_GOLD : UI_TEXT, 2, UI_RIGHT);
    }
  }
  const int fy = y0 + NUM_RACERS * rowH + 1;
  bool anyEst = false;
  for (int p = 0; p < NUM_RACERS; p++) anyEst |= !rows[p].finished;
  if (anyEst) ui_label(x + 10, fy, "* ESTIMATED", UI_DIM, 1);
  if (fast > 0) {
    gfx_fillRect(x + 92, fy + 1, 5, 5, UI_PURPLE);
    ui_label(x + 100, fy, "FASTEST LAP", UI_DIM, 1);
  }
  if (recordFlags && t > 1.0f) {
    const char* rt = (recordFlags & 3) == 3 ? "NEW LAP + RACE RECORD"
                   : (recordFlags & 2) ? "NEW RACE RECORD" : "NEW LAP RECORD";
    if ((((int)(t * 4.0f)) & 1) == 0) tag(x + w - 66, fy - 1, rt, UI_GOLD, UI_SHADOW);
  }
  if (t > 1.0f) prompt(SCR_H - 13, ui_hintConfirm(), t, 1, UI_TEXT);
}

void ui_drawResults(const ResultRow* rows, const int* points, float t) {
  ui_drawResultsEx(rows, points, 0, t);
}

void ui_drawStandingsEx(const int* order, const int* points, const int* racePts, const int* prevOrder,
                        int raceNo, int races, int nextCircuit, float t) {
  const int x = 6, y = 6, w = SCR_W - 12, h = 216;
  ui_panel(x, y, w, h, UI_ACCENT, 23);
  ui_text((float)(x + 10), (float)(y + 9), "STANDINGS", ui_styleChrome(3.0f), UI_LEFT);
  char buf[48];
  snprintf(buf, sizeof(buf), "AFTER RACE %d/%d", raceNo, races);
  ui_label(x + w - 10, y + 10, "GRAND PRIX", UI_ACCENT2, 1, UI_RIGHT);
  ui_label(x + w - 10, y + 21, buf, UI_DIM, 1, UI_RIGHT);
  int maxPts = 1;
  for (int i = 0; i < NUM_RACERS; i++) maxPts = imax(maxPts, points[i]);
  const int hy = y + 38;
  gfx_hspanMode(hy + 10, x + 6, x + w - 6, UI_EDGE, GFX_BLEND, 26);
  ui_label(x + 8, hy, "POS", UI_DIM, 1);
  ui_label(64, hy, "DRIVER", UI_DIM, 1);
  ui_label(x + w - 10, hy, "POINTS", UI_DIM, 1, UI_RIGHT);

  const int rowH = 23, y0 = hy + 15;
  const float grow = easeOut((t - 0.5f) / 0.8f);
  for (int p = 0; p < NUM_RACERS; p++) {
    const int i = order[p];
    const int ry = y0 + p * rowH;
    const int dx = rowSlide(t, p);
    if (dx >= SCR_W) continue;
    const bool me = i == PLAYER_ID;
    if (me) ui_selBar(x + 4 + dx, ry - 4, w - 8, rowH - 2, t, UI_ACCENT);
    else if (p & 1) gfx_fillRectMode(x + 4 + dx, ry - 4, w - 8, rowH - 2, COL_WHITE, GFX_BLEND, 2);
    placeCell(x + 10 + dx, ry, p + 1);
    ui_carIcon(40 + dx, ry + 2, paintOf(i));
    ui_label(64 + dx, ry, racers[i].name, me ? COL_WHITE : UI_TEXT, 2, UI_LEFT);
    // movement since the previous standings
    if (prevOrder) {
      int was = p;
      for (int k = 0; k < NUM_RACERS; k++) if (prevOrder[k] == i) was = k;
      if (was > p) arrowOutlined(134 + dx, ry + 6, 3, 0, UI_GOOD);
      else if (was < p) arrowOutlined(134 + dx, ry + 6, 3, 1, UI_BAD);
      else gfx_fillRect(131 + dx, ry + 5, 6, 2, UI_DIM);
    }
    // points bar in the racer's paint
    const int bx = 146 + dx, bw = 92;
    int len = (int)(bw * grow * points[i] / (float)maxPts);
    gfx_fillRectMode(bx, ry + 2, bw, 8, COL_BLACK, GFX_BLEND, 14);
    if (len > 0) {
      gfx_fillRect(bx, ry + 2, len, 8, paintOf(i));
      gfx_hspan(ry + 2, bx, bx + len, lerpCol(paintOf(i), COL_WHITE, 0.45f));
    }
    if (racePts && racePts[i] > 0) {
      snprintf(buf, sizeof(buf), "+%d", racePts[i]);
      ui_label(bx + bw + 5, ry + 3, buf, UI_GOOD, 1, UI_LEFT);
    }
    snprintf(buf, sizeof(buf), "%d", (int)(points[i] * grow + 0.5f));
    ui_label(x + w - 10 + dx, ry, buf, p == 0 ? UI_GOLD : UI_TEXT, 2, UI_RIGHT);
  }
  const int fy = y0 + NUM_RACERS * rowH + 1;
  if (nextCircuit >= 0 && nextCircuit < NUM_CIRCUITS) snprintf(buf, sizeof(buf), "NEXT  %s", CIRCUITS[nextCircuit].name);
  else snprintf(buf, sizeof(buf), "FINAL STANDINGS");
  ui_label(SCR_CX, fy, buf, UI_ACCENT2, 1, UI_CENTER);
  if (t > 1.0f) prompt(SCR_H - 13, ui_hintConfirm(), t, 1, UI_TEXT);
}

void ui_drawStandings(const int* order, const int* points, int raceNo, int races, float t) {
  ui_drawStandingsEx(order, points, nullptr, nullptr, raceNo, races, raceNo < races ? raceNo : -1, t);
}

// Falling confetti, deterministic from the index and the time
static void confetti(float t) {
  static const uint16_t COLS[5] = { UI_GOLD, UI_ACCENT, UI_ACCENT2, UI_GOOD, COL_WHITE };
  for (int i = 0; i < 56; i++) {
    uint32_t h = hash32((uint32_t)i * 2654435761u + 17u);
    float speed = 28.0f + (h & 31);
    float x0 = (float)((h >> 5) % SCR_W);
    float y = fmodf((float)((h >> 13) % 260) + t * speed, 260.0f) - 10.0f;
    float x = x0 + sinf(t * (1.5f + (h & 7) * 0.2f) + i) * 8.0f;
    bool flip = ((int)(t * (4.0f + (h & 3)) + i) & 1) != 0;
    gfx_fillRect((int)x, (int)y, flip ? 3 : 1, flip ? 2 : 3, COLS[(h >> 24) % 5]);
  }
}

static void podium(int cx, int top) {
  const int rx = 74, ry = 13, hgt = 22;
  for (int y = top; y < top + hgt; y++) {
    float k = (float)(y - top) / hgt;
    gfx_hspan(y, cx - rx, cx + rx + 1, lerpCol(UI_RGB(70, 60, 96), UI_RGB(20, 16, 34), k));
  }
  gfx_fillEllipse(cx, top + hgt, rx, ry, UI_RGB(20, 16, 34));
  gfx_fillEllipse(cx, top, rx, ry, UI_RGB(96, 86, 130));
  gfx_fillEllipse(cx, top, rx - 4, ry - 2, UI_RGB(120, 110, 160));
  // gold rim
  for (int dx = -rx; dx <= rx; dx++) {
    float u = (float)dx / rx;
    int yy = top + (int)(ry * sqrtf(fmaxF(0.0f, 1.0f - u * u)));
    gfx_pixel(cx + dx, yy, UI_GOLD);
  }
}

void ui_drawChampionEx(const int* order, const int* points, float t) {
  const int winner = order[0];
  drawSky(t * 12.0f, t);
  neonFloor(t);
  podium(SCR_CX, 176);
  gfx_glow(SCR_CX, 150, 90, pal.sunGlow, 10);
  showroomCar((float)SCR_CX, 178.0f, 15.0f, t * 0.9f + 2.4f, racers[winner].paint);
  confetti(t);

  float in = easeOutBack(t / 0.6f);
  UiTextStyle h = ui_styleGold(5.0f * in + 0.5f);
  h.slant = 0.2f;
  h.outlinePx = 2;
  h.shadowDx = h.shadowDy = 3;
  ui_text((float)SCR_CX, 10.0f, "CHAMPION", h, UI_CENTER);

  char buf[48];
  if (t > 0.5f) {
    UiTextStyle n = ui_styleChrome(3.0f);
    n.alpha = (uint8_t)alphaOf((t - 0.5f) / 0.3f);
    ui_text((float)SCR_CX, 56.0f, racers[winner].name, n, UI_CENTER);
    snprintf(buf, sizeof(buf), "%d POINTS", points[winner]);
    labelA(SCR_CX, 82, buf, UI_GOLD, 1, UI_CENTER, n.alpha);
  }
  if (t > 1.0f) {
    int me = 0;
    for (int p = 0; p < NUM_RACERS; p++) if (order[p] == PLAYER_ID) me = p + 1;
    if (me == 1) snprintf(buf, sizeof(buf), "YOU WIN THE GRAND PRIX!");
    else snprintf(buf, sizeof(buf), "YOU FINISHED %d%s", me, placeSuffix(me));
    ui_label(SCR_CX, 96, buf, me == 1 ? UI_GOOD : UI_TEXT, 1, UI_CENTER);
    // podium list
    shadeRect(0, SCR_H - 40, SCR_W, 26, 20);
    for (int p = 0; p < 3; p++) {
      int i = order[p];
      int cx = 56 + p * 104;
      snprintf(buf, sizeof(buf), "%d %s %d", p + 1, racers[i].name, points[i]);
      ui_carIcon(cx - 44, SCR_H - 34, paintOf(i));
      ui_label(cx - 22, SCR_H - 33, buf, placeColor(p + 1), 1, UI_LEFT);
    }
  }
  if (t > 1.5f) prompt(SCR_H - 11, ui_hintConfirm(), t, 1, UI_TEXT);
}

void ui_drawChampion(int winner, int points, float t) {
  int order[NUM_RACERS], pts[NUM_RACERS];
  for (int i = 0; i < NUM_RACERS; i++) { order[i] = i; pts[i] = 0; }
  order[0] = winner;
  order[winner] = 0;
  pts[winner] = points;
  ui_drawChampionEx(order, pts, t);
}
