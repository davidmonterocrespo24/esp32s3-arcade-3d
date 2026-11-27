/*
  ═══════════════════════════════════════════════════════════════
  TRAFFIC CAR RENDERING IMPLEMENTATION
  ═══════════════════════════════════════════════════════════════
*/

#include "render_traffic.h"
#include "rendering.h"
#include "config.h"
#include "colors.h"

// Building wall textures (64x64 RGB565)
#include "stuco_wall_texture.h"
#include "stuco_wall_window_1_texture.h"
#include "stuco_wall_window_2_texture.h"
#include "fancy_brick_wall_window_1_texture.h"
#include "fancy_brick_wall_window_2_texture.h"
#include "brick_wall_texture.h"
#include "brick_wall_window_1_texture.h"

// Texture tile scale: how many pixels of building height = one full texture tile
#define TEX_TILE_H  80   // world units per tile row

// Sample a 64x64 PROGMEM texture with tiling (u,v in 0..63 fixed range)
static inline uint16_t sampleTex(const uint16_t* tex, int u, int v) {
  u = ((u % 64) + 64) % 64;
  v = ((v % 64) + 64) % 64;
  return pgm_read_word(&tex[v * 64 + u]);
}

// Draw a textured vertical face of a building (trapezoid, scanline by scanline)
// The face goes from (xNear, yNear_bot) to (xFar, yFar_bot), height hNear/hFar
// uOffset: horizontal tile offset for uniqueness per building
static void drawTexturedWall(
    int xNear, int yNear, int hNear,
    int xFar,  int yFar,  int hFar,
    const uint16_t* tex, int uOffset, int maxy)
{
  int yTop_near = yNear - hNear;
  int yTop_far  = yFar  - hFar;

  // Vertical range to draw
  int yMin = min(yTop_near, yTop_far);
  int yMax = max(yNear, yFar);
  yMin = max(yMin, 0);
  yMax = min(yMax, maxy);
  yMax = min(yMax, SCR_H - 1);

  if (yMin >= yMax) return;

  float invH = (yMax == yMin) ? 0.0f : 1.0f / (float)(yMax - yMin);

  for (int y = yMin; y <= yMax; y++) {
    float t = (float)(y - yMin) * invH; // 0=top, 1=bottom

    // Interpolate left/right X extent for this scanline
    // near edge (bottom of face) vs far edge (top of face)
    // Actually: bottom of face = yNear/yFar, top = yTop
    // At y=yMax (bottom) t=1 → near/far bottom edge
    // At y=yMin (top) t=0 → top edge

    // Near-side x at this y
    float tNear = (yNear == yTop_near) ? 0.5f : (float)(y - yTop_near) / (float)(yNear - yTop_near);
    float tFar  = (yFar  == yTop_far)  ? 0.5f : (float)(y - yTop_far)  / (float)(yFar  - yTop_far);
    tNear = tNear < 0.0f ? 0.0f : (tNear > 1.0f ? 1.0f : tNear);
    tFar  = tFar  < 0.0f ? 0.0f : (tFar  > 1.0f ? 1.0f : tFar);

    // Horizontal blend between near and far edges
    // For the side wall, we just need: does the scanline hit both edges?
    // The wall spans from xNear (near, perspective-foreshortened) to xFar
    int xL, xR;
    // Interpolate along depth: at y, the wall runs from one perspective x to another
    // Simple: at this y, compute the two wall-edge x positions
    float depthT = t; // approximate
    int xWall = (int)(xNear + (xFar - xNear) * depthT);

    // For a side wall (one pixel wide trapezoid line), just draw a pixel
    // For a thicker side wall (perspective width), we interpolate both left/right
    // The side face is the face facing the road: it's a vertical strip at fixed x
    // Left edge = xNear-to-xFar (road side), right edge = same (it's a thin face)
    // Actually the side wall is a QUAD from near to far with zero thickness in X
    // We need to just draw one pixel per scanline along the wall

    // v texture coord: based on height position (0=top, 63=bottom)
    float heightFrac = 1.0f - tNear; // 0=bottom, 1=top → invert for texture (0=top)
    int v = (int)(heightFrac * (float)hNear / (float)TEX_TILE_H * 64.0f) % 64;
    if (v < 0) v += 64;

    int u = (uOffset + xWall / 4) % 64;
    if (u < 0) u += 64;

    uint16_t col = sampleTex(tex, u, v);
    spr.drawPixel(xWall, y, col);
  }
}

// Draw a textured filled quad (for facade and roof) — scanline fill with texture
static void drawTexturedQuad(
    int x0, int y0, int x1, int y1,
    int x2, int y2, int x3, int y3,
    const uint16_t* tex, int uOff, int vOff, int maxy)
{
  // Sort vertices top-to-bottom: we have a quad (parallelogram)
  // Use simple approach: for each scanline, find left/right x and fill
  // Vertices in order: (x0,y0),(x1,y1),(x2,y2),(x3,y3) — could be any shape

  int yMin = min(min(y0,y1),min(y2,y3));
  int yMax = max(max(y0,y1),max(y2,y3));
  yMin = max(yMin, 0);
  yMax = min(yMax, maxy);
  yMax = min(yMax, SCR_H - 1);

  if (yMin >= yMax) return;

  // For facade: it's a rectangle drawn in perspective
  // Edges: left=(x0,y0)→(x3,y3), right=(x1,y1)→(x2,y2)
  // top: (x0,y0)→(x1,y1), bottom: (x3,y3)→(x2,y2)

  for (int y = yMin; y <= yMax; y++) {
    float t = (float)(y - yMin) / (float)(yMax - yMin + 1);

    // Left edge: (x0,y0)→(x3,y3)
    float tL = (y3 == y0) ? t : (float)(y - y0) / (float)(y3 - y0);
    tL = tL < 0.0f ? 0.0f : (tL > 1.0f ? 1.0f : tL);
    int xLeft = (int)(x0 + (x3 - x0) * tL);

    // Right edge: (x1,y1)→(x2,y2)
    float tR = (y2 == y1) ? t : (float)(y - y1) / (float)(y2 - y1);
    tR = tR < 0.0f ? 0.0f : (tR > 1.0f ? 1.0f : tR);
    int xRight = (int)(x1 + (x2 - x1) * tR);

    if (xLeft > xRight) { int tmp = xLeft; xLeft = xRight; xRight = tmp; }
    if (xLeft >= SCR_W || xRight < 0) continue;
    xLeft  = max(xLeft, 0);
    xRight = min(xRight, SCR_W - 1);

    // v: based on vertical position within quad
    int v = (vOff + (int)(t * 64.0f)) % 64;
    if (v < 0) v += 64;

    for (int x = xLeft; x <= xRight; x++) {
      int u = (uOff + x / 2) % 64;
      if (u < 0) u += 64;
      spr.drawPixel(x, y, sampleTex(tex, u, v));
    }
  }
}

void drawBuilding(RenderPt& p0, RenderPt& p1, int heightVal, uint16_t baseCol, int sIdx, bool isLeft, bool showFront) {
  int h1 = (int)(p1.scale * heightVal);
  int h0 = (int)(p0.scale * heightVal);
  int bw1 = (int)(p1.scale * BUILDING_W);
  int bw0 = (int)(p0.scale * BUILDING_W);

  int off1 = p1.w * BUILDING_OFFSET;
  int off0 = p0.w * BUILDING_OFFSET;

  int x0_side  = isLeft ? (p0.x - off0) : (p0.x + off0);
  int x1_side  = isLeft ? (p1.x - off1) : (p1.x + off1);
  int x0_outer = isLeft ? (x0_side - bw0) : (x0_side + bw0);
  int x1_outer = isLeft ? (x1_side - bw1) : (x1_side + bw1);

  // Select texture based on style (7 distinct wall types)
  int style = sIdx % 7;
  const uint16_t* wallTex = nullptr;
  switch (style) {
    case 0: wallTex = stuco_wall_texture;             break; // plain stucco
    case 1: wallTex = stuco_wall_window_1_texture;    break; // stucco + window
    case 2: wallTex = stuco_wall_window_2_texture;    break; // stucco section
    case 3: wallTex = brick_wall_texture;             break; // plain brick
    case 4: wallTex = brick_wall_window_1_texture;    break; // brick + window
    case 5: wallTex = fancy_brick_wall_window_1_texture; break; // fancy brick + window
    case 6: wallTex = fancy_brick_wall_window_2_texture; break; // fancy brick plain
    default: wallTex = stuco_wall_texture; break;
  }

  int uOff = (sIdx * 13) % 64; // unique horizontal offset per building block
  int maxy = p0.y; // clip to road horizon

  // 1. SIDE WALL — textured trapezoid in perspective
  // 4 screen vertices:
  //   BL=(x0_side, p0.y),      TL=(x0_side, p0.y-h0)   ← near column (fixed X)
  //   BR=(x1_side, p1.y),      TR=(x1_side, p1.y-h1)   ← far column  (fixed X)
  // For each scanline y, find which X on the left edge and right edge is active,
  // then fill that horizontal span with texture.
  if (h0 > 2 && h1 > 2) {
    // Trapezoid has 4 edges. For scanline fill we track two "active" edges at a time.
    // Top edge:    TL(x0_side, p0.y-h0) → TR(x1_side, p1.y-h1)
    // Bottom edge: BL(x0_side, p0.y)    → BR(x1_side, p1.y)
    // Left edge:   TL → BL   (vertical line at x = x0_side)
    // Right edge:  TR → BR   (vertical line at x = x1_side)
    //
    // On screen this trapezoid is a perspective quad:
    // The left column is at screen-x = x0_side (near, wider/lower)
    // The right column is at screen-x = x1_side (far,  narrower/higher)
    // The top boundary goes from y=(p0.y-h0) on the left to y=(p1.y-h1) on the right
    // The bottom boundary goes from y=p0.y    on the left to y=p1.y     on the right
    //
    // For each scanline y:
    //   - Find the x range along the TOP edge at this y    → one boundary
    //   - Find the x range along the BOTTOM edge at this y → other boundary
    //   This doesn't work simply; instead do a proper scanline:
    //   For each x in [x0_side..x1_side], find the y range of the wall column at that x.

    int xA = x0_side;  // near column x
    int xB = x1_side;  // far column x
    if (xA > xB) { int t = xA; xA = xB; xB = t; }
    xA = max(xA, 0);
    xB = min(xB, SCR_W - 1);

    for (int x = xA; x <= xB; x++) {
      // How far along [x0_side..x1_side] is this column? (0=near, 1=far)
      float tX;
      int spanX = x1_side - x0_side;
      if (spanX == 0) tX = 0.5f;
      else tX = (float)(x - x0_side) / (float)spanX;
      tX = tX < 0.0f ? 0.0f : (tX > 1.0f ? 1.0f : tX);

      // Interpolate top and bottom y for this column
      int yTop = (int)((p0.y - h0) + tX * ((p1.y - h1) - (p0.y - h0)));
      int yBot = (int)(p0.y        + tX * (p1.y         - p0.y));

      yTop = max(yTop, 0);
      yBot = min(yBot, maxy);
      yBot = min(yBot, SCR_H - 1);
      if (yTop >= yBot) continue;

      // u: based on depth (near→far = 0→64), tiled
      int u = (uOff + (int)(tX * 64.0f)) % 64;
      if (u < 0) u += 64;

      int colH = yBot - yTop;
      for (int y = yTop; y <= yBot; y++) {
        // v: vertical fraction within this column (0=top, 1=bottom)
        float vFrac = (colH == 0) ? 0.0f : (float)(y - yTop) / (float)colH;
        // tile vertically: one tile = TEX_TILE_H screen pixels
        int v = (int)(vFrac * (float)colH / (float)TEX_TILE_H * 64.0f) % 64;
        if (v < 0) v += 64;

        uint16_t col = sampleTex(wallTex, u, v);
        // Darken side face (×0.6) to give depth vs front facade
        uint8_t r = ((col >> 11) & 0x1F) * 3 / 5;
        uint8_t g = ((col >> 5)  & 0x3F) * 3 / 5;
        uint8_t b = (col & 0x1F)         * 3 / 5;
        spr.drawPixel(x, y, (r << 11) | (g << 5) | b);
      }
    }
  }

  // 2. ROOF (solid color, slightly darker)
  drawQuad(x0_side, p0.y - h0, x1_side, p1.y - h1,
           x1_outer, p1.y - h1, x0_outer, p0.y - h0,
           darkenCol(baseCol, 0.85));

  // 3. FRONT FACADE (textured, only at block start)
  if (showFront && h0 > 4 && bw0 > 4) {
    // Facade quad: (x0_side, p0.y) → (x0_outer, p0.y) → (x0_outer, p0.y-h0) → (x0_side, p0.y-h0)
    drawTexturedQuad(
      x0_side, p0.y,
      x0_outer, p0.y,
      x0_outer, p0.y - h0,
      x0_side, p0.y - h0,
      wallTex, uOff, 0, maxy
    );
  }
}
