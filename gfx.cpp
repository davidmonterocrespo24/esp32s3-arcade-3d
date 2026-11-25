/*
  ═══════════════════════════════════════════════════════════════
  SOFTWARE FRAMEBUFFER + RASTERIZER IMPLEMENTATION
  ═══════════════════════════════════════════════════════════════
*/

// Arduino-ESP32 builds the sketch with -Os; the raster loops want -O2.
#pragma GCC optimize("O2")

#include "gfx.h"
#include "colors.h"
#include <math.h>
#include <string.h>

uint16_t* gfx_fb = nullptr;
int gfx_cx0 = 0, gfx_cy0 = 0, gfx_cx1 = SCR_W, gfx_cy1 = SCR_H;
static bool sFbInternal = false;

typedef uint32_t __attribute__((__may_alias__)) u32a;

// ═══════════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════════
bool gfx_init() {
  const size_t bytes = (size_t)SCR_W * SCR_H * sizeof(uint16_t);
  // Internal SRAM first: every fill is a burst of writes, and internal
  // SRAM costs no cache traffic. PSRAM is the fallback, not the default.
  gfx_fb = (uint16_t*)plat_alloc_internal(bytes);
  sFbInternal = (gfx_fb != nullptr);
  if (!gfx_fb) gfx_fb = (uint16_t*)plat_alloc_psram(bytes);
  if (!gfx_fb) return false;
  plat_log("gfx: %ux%u framebuffer (%u B) in %s\n", SCR_W, SCR_H,
           (unsigned)bytes, sFbInternal ? "internal SRAM" : "PSRAM");
  gfx_resetClip();
  gfx_clear(0);
  return true;
}

bool gfx_fbInternal() { return sFbInternal; }

void gfx_setClip(int x0, int y0, int x1, int y1) {
  gfx_cx0 = x0 < 0 ? 0 : x0;
  gfx_cy0 = y0 < 0 ? 0 : y0;
  gfx_cx1 = x1 > SCR_W ? SCR_W : x1;
  gfx_cy1 = y1 > SCR_H ? SCR_H : y1;
}

void gfx_resetClip() {
  gfx_cx0 = 0; gfx_cy0 = 0; gfx_cx1 = SCR_W; gfx_cy1 = SCR_H;
}

// ═══════════════════════════════════════════════════════════════
//  SPANS
// ═══════════════════════════════════════════════════════════════
static inline void fill16(uint16_t* p, int n, uint16_t c) {
  if (n <= 0) return;
  if ((uintptr_t)p & 2) { *p++ = c; n--; }
  uint32_t c2 = c | ((uint32_t)c << 16);
  u32a* q = (u32a*)p;
  int m = n >> 1;
  while (m >= 4) { q[0] = c2; q[1] = c2; q[2] = c2; q[3] = c2; q += 4; m -= 4; }
  while (m-- > 0) *q++ = c2;
  if (n & 1) *(uint16_t*)q = c;
}

static inline void blendRun(uint16_t* p, int n, uint16_t c, uint32_t a) {
  uint32_t f = (c | ((uint32_t)c << 16)) & 0x07E0F81Fu;
  for (int i = 0; i < n; i++) {
    uint32_t b = (p[i] | ((uint32_t)p[i] << 16)) & 0x07E0F81Fu;
    uint32_t r = ((((f - b) * a) >> 5) + b) & 0x07E0F81Fu;
    p[i] = (uint16_t)(r | (r >> 16));
  }
}

static inline void addRun(uint16_t* p, int n, uint16_t c) {
  for (int i = 0; i < n; i++) p[i] = add565(p[i], c);
}

void gfx_clear(uint16_t c) {
  fill16(gfx_fb, SCR_W * SCR_H, c);
}

void HOT gfx_hspan(int y, int x0, int x1, uint16_t c) {
  if (y < gfx_cy0 || y >= gfx_cy1) return;
  if (x0 < gfx_cx0) x0 = gfx_cx0;
  if (x1 > gfx_cx1) x1 = gfx_cx1;
  if (x1 <= x0) return;
  fill16(gfx_fb + y * SCR_W + x0, x1 - x0, c);
}

void HOT gfx_hspanMode(int y, int x0, int x1, uint16_t c, int mode, int alpha32) {
  if (y < gfx_cy0 || y >= gfx_cy1) return;
  if (x0 < gfx_cx0) x0 = gfx_cx0;
  if (x1 > gfx_cx1) x1 = gfx_cx1;
  if (x1 <= x0) return;
  uint16_t* p = gfx_fb + y * SCR_W + x0;
  if (mode == GFX_SOLID || alpha32 >= 32) {
    if (mode == GFX_ADD) addRun(p, x1 - x0, c);
    else fill16(p, x1 - x0, c);
  } else if (mode == GFX_BLEND) {
    if (alpha32 > 0) blendRun(p, x1 - x0, c, (uint32_t)alpha32);
  } else {
    addRun(p, x1 - x0, scale565(c, (uint32_t)(alpha32 < 0 ? 0 : alpha32)));
  }
}

void gfx_vspan(int x, int y0, int y1, uint16_t c) {
  if (x < gfx_cx0 || x >= gfx_cx1) return;
  if (y0 < gfx_cy0) y0 = gfx_cy0;
  if (y1 > gfx_cy1) y1 = gfx_cy1;
  uint16_t* p = gfx_fb + y0 * SCR_W + x;
  for (int y = y0; y < y1; y++, p += SCR_W) *p = c;
}

void gfx_fillRect(int x, int y, int w, int h, uint16_t c) {
  int y1 = y + h;
  if (y < gfx_cy0) y = gfx_cy0;
  if (y1 > gfx_cy1) y1 = gfx_cy1;
  for (; y < y1; y++) gfx_hspan(y, x, x + w, c);
}

void gfx_fillRectMode(int x, int y, int w, int h, uint16_t c, int mode, int alpha32) {
  int y1 = y + h;
  if (y < gfx_cy0) y = gfx_cy0;
  if (y1 > gfx_cy1) y1 = gfx_cy1;
  for (; y < y1; y++) gfx_hspanMode(y, x, x + w, c, mode, alpha32);
}

void gfx_rect(int x, int y, int w, int h, uint16_t c) {
  if (w <= 0 || h <= 0) return;
  gfx_hspan(y, x, x + w, c);
  gfx_hspan(y + h - 1, x, x + w, c);
  gfx_vspan(x, y, y + h, c);
  gfx_vspan(x + w - 1, y, y + h, c);
}

void gfx_pixel(int x, int y, uint16_t c) {
  if (x < gfx_cx0 || x >= gfx_cx1 || y < gfx_cy0 || y >= gfx_cy1) return;
  gfx_fb[y * SCR_W + x] = c;
}

void gfx_pixelMode(int x, int y, uint16_t c, int mode, int alpha32) {
  if (x < gfx_cx0 || x >= gfx_cx1 || y < gfx_cy0 || y >= gfx_cy1) return;
  uint16_t* p = gfx_fb + y * SCR_W + x;
  if (mode == GFX_BLEND) *p = blend565(c, *p, (uint32_t)alpha32);
  else if (mode == GFX_ADD) *p = add565(*p, alpha32 >= 32 ? c : scale565(c, (uint32_t)alpha32));
  else *p = c;
}

void gfx_line(int x0, int y0, int x1, int y1, uint16_t c) {
  int dx = x1 > x0 ? x1 - x0 : x0 - x1;
  int dy = y1 > y0 ? y0 - y1 : y1 - y0;   // negative
  int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (int guard = 0; guard < 2048; guard++) {
    gfx_pixel(x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

// ═══════════════════════════════════════════════════════════════
//  CONVEX POLYGONS
// ═══════════════════════════════════════════════════════════════
void HOT gfx_fillPoly(const float* xy, int n, uint16_t c, int mode, int alpha32) {
  if (n < 3 || n > 8) return;
  float minY = 1e30f, maxY = -1e30f, minX = 1e30f, maxX = -1e30f;
  for (int i = 0; i < n; i++) {
    float x = xy[2 * i], y = xy[2 * i + 1];
    if (!(x == x) || !(y == y)) return;               // NaN guard
    if (y < minY) minY = y;
    if (y > maxY) maxY = y;
    if (x < minX) minX = x;
    if (x > maxX) maxX = x;
  }
  if (maxX < gfx_cx0 - 1 || minX > gfx_cx1 + 1) return;
  if (maxY < gfx_cy0 - 1 || minY > gfx_cy1 + 1) return;

  struct Edge { float ya, yb, xa, k; } e[8];
  int ne = 0;
  for (int i = 0, j = n - 1; i < n; j = i++) {
    float ax = xy[2 * j], ay = xy[2 * j + 1];
    float bx = xy[2 * i], by = xy[2 * i + 1];
    if (ay == by) continue;
    if (ay > by) { float t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; }
    e[ne].ya = ay; e[ne].yb = by; e[ne].xa = ax; e[ne].k = (bx - ax) / (by - ay);
    ne++;
  }

  float cMinY = minY < gfx_cy0 - 1 ? (float)(gfx_cy0 - 1) : minY;
  float cMaxY = maxY > gfx_cy1 + 1 ? (float)(gfx_cy1 + 1) : maxY;
  int y0 = (int)ceilf(cMinY - 0.5f);
  int y1 = (int)ceilf(cMaxY - 0.5f);
  if (y0 < gfx_cy0) y0 = gfx_cy0;
  if (y1 > gfx_cy1) y1 = gfx_cy1;

  for (int y = y0; y < y1; y++) {
    float yc = y + 0.5f, xl = 1e30f, xr = -1e30f;
    for (int k = 0; k < ne; k++) {
      if (yc >= e[k].ya && yc < e[k].yb) {
        float x = e[k].xa + (yc - e[k].ya) * e[k].k;
        if (x < xl) xl = x;
        if (x > xr) xr = x;
      }
    }
    if (xl >= xr) continue;
    if (xl < gfx_cx0 - 1) xl = (float)(gfx_cx0 - 1);
    if (xr > gfx_cx1 + 1) xr = (float)(gfx_cx1 + 1);
    int xa = (int)ceilf(xl - 0.5f), xb = (int)ceilf(xr - 0.5f);
    if (mode == GFX_SOLID) gfx_hspan(y, xa, xb, c);
    else gfx_hspanMode(y, xa, xb, c, mode, alpha32);
  }
}

void gfx_fillQuad(float x0, float y0, float x1, float y1,
                  float x2, float y2, float x3, float y3,
                  uint16_t c, int mode, int alpha32) {
  float v[8] = { x0, y0, x1, y1, x2, y2, x3, y3 };
  gfx_fillPoly(v, 4, c, mode, alpha32);
}

void gfx_fillTri(float x0, float y0, float x1, float y1, float x2, float y2,
                 uint16_t c, int mode, int alpha32) {
  float v[6] = { x0, y0, x1, y1, x2, y2 };
  gfx_fillPoly(v, 3, c, mode, alpha32);
}

// ═══════════════════════════════════════════════════════════════
//  CIRCLES / ELLIPSES / GLOWS
// ═══════════════════════════════════════════════════════════════
void gfx_fillCircle(int cx, int cy, int r, uint16_t c) {
  gfx_fillEllipse(cx, cy, r, r, c, GFX_SOLID, 32);
}

void gfx_circle(int cx, int cy, int r, uint16_t c) {
  int x = r, y = 0, err = 1 - r;
  while (x >= y) {
    gfx_pixel(cx + x, cy + y, c); gfx_pixel(cx - x, cy + y, c);
    gfx_pixel(cx + x, cy - y, c); gfx_pixel(cx - x, cy - y, c);
    gfx_pixel(cx + y, cy + x, c); gfx_pixel(cx - y, cy + x, c);
    gfx_pixel(cx + y, cy - x, c); gfx_pixel(cx - y, cy - x, c);
    y++;
    if (err < 0) err += 2 * y + 1;
    else { x--; err += 2 * (y - x) + 1; }
  }
}

void gfx_fillEllipse(int cx, int cy, int rx, int ry, uint16_t c, int mode, int alpha32) {
  if (rx <= 0 || ry <= 0) return;
  float inv = 1.0f / ((float)ry + 0.5f);
  for (int dy = -ry; dy <= ry; dy++) {
    float t = dy * inv;
    float s = 1.0f - t * t;
    if (s <= 0) continue;
    int hw = (int)(((float)rx + 0.5f) * sqrtf(s));
    gfx_hspanMode(cy + dy, cx - hw, cx + hw + 1, c, mode, alpha32);
  }
}

// Falloff (1 - d^2/r^2)^2 * 32, indexed by d^2/r^2 in 1/256 steps
static uint8_t sGlowLUT[257];
static bool sGlowInit = false;

void HOT gfx_glow(int cx, int cy, int r, uint16_t c, int strength32) {
  if (r <= 0 || strength32 <= 0) return;
  if (!sGlowInit) {
    for (int i = 0; i <= 256; i++) {
      float f = 1.0f - i / 256.0f;
      sGlowLUT[i] = (uint8_t)(f * f * 32.0f + 0.5f);
    }
    sGlowInit = true;
  }
  if (strength32 > 32) strength32 = 32;
  // the 33 possible light colours, so the pixel loop is lookup + add
  uint16_t shade[33];
  for (int a = 0; a <= 32; a++) shade[a] = scale565(c, (uint32_t)((a * strength32) >> 5));
  int r2 = r * r;
  uint32_t k = (256u << 16) / (uint32_t)r2;      // d^2 -> LUT index, 16.16
  int y0 = cy - r, y1 = cy + r + 1;
  if (y0 < gfx_cy0) y0 = gfx_cy0;
  if (y1 > gfx_cy1) y1 = gfx_cy1;
  for (int y = y0; y < y1; y++) {
    int dy = y - cy, dy2 = dy * dy;
    int hw = (int)sqrtf((float)(r2 - dy2));
    int x0 = cx - hw, x1 = cx + hw + 1;
    if (x0 < gfx_cx0) x0 = gfx_cx0;
    if (x1 > gfx_cx1) x1 = gfx_cx1;
    uint16_t* p = gfx_fb + y * SCR_W;
    for (int x = x0; x < x1; x++) {
      int dx = x - cx;
      uint32_t idx = ((uint32_t)(dx * dx + dy2) * k) >> 16;
      if (idx >= 256) continue;
      uint8_t a = sGlowLUT[idx];
      if (a) p[x] = add565(p[x], shade[a]);
    }
  }
}

// ═══════════════════════════════════════════════════════════════
//  ROW REGIONS (road rows) AND DITHERED GRADIENT ROWS
// ═══════════════════════════════════════════════════════════════
void HOT gfx_rowRegions(int y, const float* b, const uint16_t* cols, int nr) {
  if (y < gfx_cy0 || y >= gfx_cy1 || nr <= 0) return;
  uint16_t* row = gfx_fb + y * SCR_W;
  int xs = gfx_cx0;
  float prevE = (float)gfx_cx0;
  for (int r = 0; r < nr && xs < gfx_cx1; r++) {
    float e = (r < nr - 1) ? b[r] : (float)gfx_cx1;
    if (e < prevE) e = prevE;                 // keep boundaries monotonic
    prevE = e;
    // Region ends before the first unwritten pixel: nothing of it is visible.
    // (Blending here would paint x=0 with the NEXT region's colour.)
    if (e <= (float)xs) continue;
    if (e > (float)gfx_cx1) e = (float)gfx_cx1;
    int ef = (int)e;                           // e > xs >= 0, so trunc == floor
    if (ef > xs) { fill16(row + xs, ef - xs, cols[r]); xs = ef; }
    if (r < nr - 1 && ef == xs && ef < gfx_cx1) {
      // boundary pixel: fraction (e - ef) belongs to region r
      uint32_t a = (uint32_t)((e - ef) * 32.0f + 0.5f);
      row[ef] = blend565(cols[r], cols[r + 1], a);
      xs = ef + 1;
    }
  }
  if (xs < gfx_cx1) fill16(row + xs, gfx_cx1 - xs, cols[nr - 1]);
}

// 4x4 Bayer thresholds
static const uint8_t BAYER4[4][4] = {
  { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 }
};

void gfx_gradientRow(int y, int x0, int x1, float r8, float g8, float b8) {
  if (y < gfx_cy0 || y >= gfx_cy1) return;
  if (x0 < gfx_cx0) x0 = gfx_cx0;
  if (x1 > gfx_cx1) x1 = gfx_cx1;
  if (x1 <= x0) return;
  float r = r8 * (31.0f / 255.0f), g = g8 * (63.0f / 255.0f), bl = b8 * (31.0f / 255.0f);
  uint16_t pat[4];
  for (int k = 0; k < 4; k++) {
    float th = (BAYER4[y & 3][k] + 0.5f) * (1.0f / 16.0f);
    int ri = (int)(r + th), gi = (int)(g + th), bi = (int)(bl + th);
    if (ri > 31) ri = 31;
    if (gi > 63) gi = 63;
    if (bi > 31) bi = 31;
    pat[k] = (uint16_t)((ri << 11) | (gi << 5) | bi);
  }
  uint16_t* p = gfx_fb + y * SCR_W;
  for (int x = x0; x < x1; x++) p[x] = pat[x & 3];
}

void gfx_ditherRow(int y, int x0, int x1, uint16_t c0, uint16_t c1, float t) {
  float r0 = ((c0 >> 11) & 31) * (255.0f / 31.0f), r1 = ((c1 >> 11) & 31) * (255.0f / 31.0f);
  float g0 = ((c0 >> 5) & 63) * (255.0f / 63.0f), g1 = ((c1 >> 5) & 63) * (255.0f / 63.0f);
  float b0 = (c0 & 31) * (255.0f / 31.0f), b1 = (c1 & 31) * (255.0f / 31.0f);
  gfx_gradientRow(y, x0, x1, r0 + (r1 - r0) * t, g0 + (g1 - g0) * t, b0 + (b1 - b0) * t);
}

// ═══════════════════════════════════════════════════════════════
//  5x7 FONT (ASCII 32..95; lowercase is drawn as uppercase)
//  Each glyph: 7 rows, bit 4 = leftmost column.
// ═══════════════════════════════════════════════════════════════
static const uint8_t FONT5x7[64][7] = {
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ' '
  {0x04,0x04,0x04,0x04,0x04,0x00,0x04}, // !
  {0x0A,0x0A,0x00,0x00,0x00,0x00,0x00}, // "
  {0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00}, // #
  {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}, // $
  {0x19,0x19,0x02,0x04,0x08,0x13,0x13}, // %
  {0x0C,0x12,0x14,0x08,0x15,0x12,0x0D}, // &
  {0x04,0x04,0x00,0x00,0x00,0x00,0x00}, // '
  {0x02,0x04,0x08,0x08,0x08,0x04,0x02}, // (
  {0x08,0x04,0x02,0x02,0x02,0x04,0x08}, // )
  {0x00,0x04,0x15,0x0E,0x15,0x04,0x00}, // *
  {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, // +
  {0x00,0x00,0x00,0x00,0x0C,0x04,0x08}, // ,
  {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, // -
  {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}, // .
  {0x01,0x01,0x02,0x04,0x08,0x10,0x10}, // /
  {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, // 0
  {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, // 1
  {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F}, // 2
  {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}, // 3
  {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, // 4
  {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, // 5
  {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, // 6
  {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, // 7
  {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, // 8
  {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, // 9
  {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00}, // :
  {0x00,0x0C,0x0C,0x00,0x0C,0x04,0x08}, // ;
  {0x02,0x04,0x08,0x10,0x08,0x04,0x02}, // <
  {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, // =
  {0x08,0x04,0x02,0x01,0x02,0x04,0x08}, // >
  {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}, // ?
  {0x0E,0x11,0x17,0x15,0x17,0x10,0x0F}, // @
  {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, // A
  {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, // B
  {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, // C
  {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, // D
  {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, // E
  {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, // F
  {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, // G
  {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, // H
  {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, // I
  {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, // J
  {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, // K
  {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, // L
  {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, // M
  {0x11,0x11,0x19,0x15,0x13,0x11,0x11}, // N
  {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, // O
  {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, // P
  {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, // Q
  {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, // R
  {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, // S
  {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, // T
  {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, // U
  {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, // V
  {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, // W
  {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, // X
  {0x11,0x11,0x11,0x0A,0x04,0x04,0x04}, // Y
  {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, // Z
  {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}, // [
  {0x10,0x10,0x08,0x04,0x02,0x01,0x01}, // backslash
  {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}, // ]
  {0x04,0x0A,0x11,0x00,0x00,0x00,0x00}, // ^
  {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}  // _
};

int gfx_textWidth(const char* s, int scale) {
  int n = 0;
  while (s[n]) n++;
  return n > 0 ? (n * 6 - 1) * scale : 0;
}

void gfx_text(int x, int y, const char* s, uint16_t c, int scale) {
  int x0 = x;
  for (; *s; s++) {
    char ch = *s;
    if (ch == '\n') { x = x0; y += 8 * scale; continue; }
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch < 32 || ch > 95) ch = '?';
    const uint8_t* g = FONT5x7[ch - 32];
    for (int r = 0; r < 7; r++) {
      uint8_t bits = g[r];
      if (!bits) continue;
      for (int col = 0; col < 5; col++) {
        if (bits & (0x10 >> col)) {
          if (scale == 1) gfx_pixel(x + col, y + r, c);
          else gfx_fillRect(x + col * scale, y + r * scale, scale, scale, c);
        }
      }
    }
    x += 6 * scale;
  }
}

void gfx_textShadow(int x, int y, const char* s, uint16_t c, int scale, uint16_t shadow) {
  int o = scale > 1 ? scale / 2 + 1 : 1;
  gfx_text(x + o, y + o, s, shadow, scale);
  gfx_text(x, y, s, c, scale);
}

void gfx_textCentered(int cx, int y, const char* s, uint16_t c, int scale, uint16_t shadow) {
  gfx_textShadow(cx - gfx_textWidth(s, scale) / 2, y, s, c, scale, shadow);
}
