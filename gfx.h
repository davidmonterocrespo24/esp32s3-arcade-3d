/*
  ═══════════════════════════════════════════════════════════════
  SOFTWARE FRAMEBUFFER + RASTERIZER
  Replaces TFT_eSprite. Everything draws straight into one RGB565
  buffer (native endian) that lives in internal SRAM when it fits.
  The display layer byte-swaps it into DMA bands on the other core.

  Rules shared by all fills: a pixel is covered when its CENTRE is
  inside the shape (top-left convention), so neighbouring polygons
  never leave cracks or double-draw a seam.
  ═══════════════════════════════════════════════════════════════
*/

#ifndef GFX_H
#define GFX_H

#include <stdint.h>
#include "config.h"
#include "platform.h"

extern uint16_t* gfx_fb;            // SCR_W * SCR_H pixels
extern int gfx_cx0, gfx_cy0, gfx_cx1, gfx_cy1;   // clip: [x0,x1) x [y0,y1)

bool gfx_init();
bool gfx_fbInternal();

void gfx_setClip(int x0, int y0, int x1, int y1);
void gfx_resetClip();
static inline void gfx_setClipBottom(int y) {
  gfx_cy1 = (y < 0) ? 0 : (y > SCR_H ? SCR_H : y);
}

enum GfxMode { GFX_SOLID = 0, GFX_BLEND = 1, GFX_ADD = 2 };

void gfx_clear(uint16_t c);
void gfx_hspan(int y, int x0, int x1, uint16_t c);                 // [x0, x1)
void gfx_hspanMode(int y, int x0, int x1, uint16_t c, int mode, int alpha32);
void gfx_vspan(int x, int y0, int y1, uint16_t c);                 // [y0, y1)
void gfx_fillRect(int x, int y, int w, int h, uint16_t c);
void gfx_fillRectMode(int x, int y, int w, int h, uint16_t c, int mode, int alpha32);
void gfx_rect(int x, int y, int w, int h, uint16_t c);
void gfx_pixel(int x, int y, uint16_t c);
void gfx_pixelMode(int x, int y, uint16_t c, int mode, int alpha32);
void gfx_line(int x0, int y0, int x1, int y1, uint16_t c);

// Convex polygon, any winding, float vertices (xy pairs), up to 8 vertices.
void gfx_fillPoly(const float* xy, int n, uint16_t c, int mode = GFX_SOLID, int alpha32 = 32);
void gfx_fillQuad(float x0, float y0, float x1, float y1,
                  float x2, float y2, float x3, float y3,
                  uint16_t c, int mode = GFX_SOLID, int alpha32 = 32);
void gfx_fillTri(float x0, float y0, float x1, float y1, float x2, float y2,
                 uint16_t c, int mode = GFX_SOLID, int alpha32 = 32);

void gfx_fillCircle(int cx, int cy, int r, uint16_t c);
void gfx_circle(int cx, int cy, int r, uint16_t c);
void gfx_fillEllipse(int cx, int cy, int rx, int ry, uint16_t c,
                     int mode = GFX_SOLID, int alpha32 = 32);
// Additive radial light: brightest at the centre, fades to 0 at r.
void gfx_glow(int cx, int cy, int r, uint16_t c, int strength32 = 32);

// One screen row split into colour regions at fractional x boundaries,
// with the boundary pixels blended by coverage (cheap 1-px anti-aliasing).
// bounds[nRegions-1] must be ascending; regions extend to the clip edges.
void gfx_rowRegions(int y, const float* bounds, const uint16_t* cols, int nRegions);

// Ordered-dithered (4x4 Bayer) row of one colour given in 8-bit floats:
// smooth gradients without RGB565 banding. ditherRow mixes c0..c1 by t.
void gfx_gradientRow(int y, int x0, int x1, float r8, float g8, float b8);
void gfx_ditherRow(int y, int x0, int x1, uint16_t c0, uint16_t c1, float t);

// 5x7 bitmap font, 6 px advance per character at scale 1.
int  gfx_textWidth(const char* s, int scale);
void gfx_text(int x, int y, const char* s, uint16_t c, int scale);
void gfx_textShadow(int x, int y, const char* s, uint16_t c, int scale, uint16_t shadow);
void gfx_textCentered(int cx, int y, const char* s, uint16_t c, int scale, uint16_t shadow);

#endif // GFX_H
