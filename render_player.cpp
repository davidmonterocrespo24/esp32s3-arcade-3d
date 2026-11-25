/*
  ═══════════════════════════════════════════════════════════════
  3D CAR RENDERER
  OBJ mesh (car2_mesh.h) + 128x128 RGB565 texture (car2_texture.h)

  What changed vs. the first version (and why the car looked "crooked"):
  - Yaw came from the lateral POSITION (rotY = playerX * 0.5): any time
    the car was off-centre, e.g. the autopilot in every curve, it was
    drawn turned up to 23 degrees. Yaw now follows the STEERING.
  - The eye sat 6.5 units from a 6.5-unit-long model: the rear bumper
    was 3x nearer than the nose, so the car looked like a wedge. The
    camera now sits further back with a longer focal length.
  - The shadow moved with playerX while the car stayed centred.
  - Affine texturing with integer edges: textures swam and adjacent
    triangles cracked; scanlines > 160 px and triangles near the screen
    edge were silently skipped. Now: perspective-correct UVs (exact
    every 8 px), pixel-centre edges, proper clipping.
  - Flat per-face light in OBJECT space (it never changed when the car
    turned). Now: smoothed normals rotated with the car, Gouraud
    diffuse + specular + sky reflection, coloured by time of day.
  - spr.drawPixel() per texel (bounds check + call per pixel, into
    PSRAM). Now direct writes into the internal-SRAM framebuffer, with
    the texture copied to internal RAM (not read from flash via cache).
  ═══════════════════════════════════════════════════════════════
*/

#pragma GCC optimize("O2")

#include <Arduino.h>      // PROGMEM for the texture header
#include "render_player.h"
#include "rendering.h"
#include "config.h"
#include "physics.h"
#include "track.h"
#include "utils.h"
#include "render_road.h"
#include "car2_mesh.h"
#include "car2_texture.h"
#include <math.h>
#include <string.h>

#define TEX_W CAR2_TEX_W
#define TEX_H CAR2_TEX_H
#define PAINT_FLAG 0x0020    // green LSB of a texel marks car paint

static uint16_t* sTex = nullptr;
static const float MODEL_CZ = -0.03f;    // model centre along z (front is +z)

// Per-vertex work arrays (internal .bss, ~16 KB)
struct VOut { float x, y, iz, depth, px, py; int16_t lr, lg, lb, sr, sg, sb; uint8_t lit; };
static VOut sV[car2_vert_count];
static uint16_t sOrder[car2_tri_count];
static float    sDepth[car2_tri_count];
static uint16_t sPaintLUT[64];

void initCarRenderer() {
  const size_t bytes = TEX_W * TEX_H * sizeof(uint16_t);
  sTex = (uint16_t*)plat_alloc_internal(bytes);
  if (!sTex) sTex = (uint16_t*)plat_alloc_psram(bytes);
  if (!sTex) return;
  for (int i = 0; i < TEX_W * TEX_H; i++) {
    uint16_t c = car2_texture[i];
    int r = ((c >> 11) & 31) * 255 / 31, g = ((c >> 5) & 63) * 255 / 63, b = (c & 31) * 255 / 31;
    int dg = r - g; if (dg < 0) dg = -dg;
    bool paint = (b > r + 15) && (dg < 14) && (b > 40) && (b < 150);
    sTex[i] = (uint16_t)((c & ~PAINT_FLAG) | (paint ? PAINT_FLAG : 0));
  }
}

static void buildPaintLUT(uint16_t paint) {
  // Paint luminance in the texture averages ~11.5 on this 0..63 scale.
  const float ref = 11.5f;
  for (int l = 0; l < 64; l++) {
    if (l <= ref) sPaintLUT[l] = scale565(paint, (uint32_t)(l / ref * 30.0f + 0.5f));
    else sPaintLUT[l] = blend565(COL_WHITE, paint, (uint32_t)((l - ref) / (63 - ref) * 24.0f));
  }
}

// ═══════════════════════════════════════════════════════════════
//  TRIANGLE RASTERIZER: perspective-correct UV, Gouraud light + spec
// ═══════════════════════════════════════════════════════════════
// Attributes: 0 iz, 1 uz, 2 vz (perspective), 3-5 diffuse rgb, 6-8 additive rgb.
// All are linear in screen space, so their x/y gradients are constant over the
// triangle: computed ONCE per triangle from the plane equation, then each
// scanline only needs its two edge x positions (no per-row interpolation of
// 11 values and no per-row division, which is what dominated small triangles).
#define NATTR 9
struct RV { float x, y; float a[NATTR]; };

static void HOT rasterTri(const RV* p0, const RV* p1, const RV* p2, bool usePaint) {
  float x0 = p0->x, y0 = p0->y;
  float e1x = p1->x - x0, e1y = p1->y - y0, e2x = p2->x - x0, e2y = p2->y - y0;
  float det = e1x * e2y - e2x * e1y;
  if (det > -0.05f && det < 0.05f) return;          // degenerate
  float idet = 1.0f / det;
  float gx[NATTR], gy[NATTR];
  for (int k = 0; k < NATTR; k++) {
    float d1 = p1->a[k] - p0->a[k], d2 = p2->a[k] - p0->a[k];
    gx[k] = (d1 * e2y - d2 * e1y) * idet;
    gy[k] = (d2 * e1x - d1 * e2x) * idet;
  }
  const float FX = 65536.0f;
  int32_t dlr = (int32_t)(gx[3] * FX), dlg = (int32_t)(gx[4] * FX), dlb = (int32_t)(gx[5] * FX);
  int32_t dsr = (int32_t)(gx[6] * FX), dsg = (int32_t)(gx[7] * FX), dsb = (int32_t)(gx[8] * FX);

  // Edge walking on positions only
  const RV *a = p0, *b = p1, *c = p2, *t;
  if (a->y > b->y) { t = a; a = b; b = t; }
  if (a->y > c->y) { t = a; a = c; c = t; }
  if (b->y > c->y) { t = b; b = c; c = t; }
  float dyAC = c->y - a->y;
  if (dyAC < 1e-3f) return;
  float sAC = (c->x - a->x) / dyAC;
  float dyAB = b->y - a->y, dyBC = c->y - b->y;
  float sAB = dyAB > 1e-4f ? (b->x - a->x) / dyAB : 0.0f;
  float sBC = dyBC > 1e-4f ? (c->x - b->x) / dyBC : 0.0f;
  int yStart = (int)ceilf(a->y - 0.5f), yEnd = (int)ceilf(c->y - 0.5f);
  if (yStart < gfx_cy0) yStart = gfx_cy0;
  if (yEnd > gfx_cy1) yEnd = gfx_cy1;

  for (int y = yStart; y < yEnd; y++) {
    float yc = y + 0.5f;
    float xa = a->x + (yc - a->y) * sAC;
    float xb = (yc < b->y) ? a->x + (yc - a->y) * sAB : b->x + (yc - b->y) * sBC;
    float xl = xa < xb ? xa : xb, xr = xa < xb ? xb : xa;
    int xs = (int)ceilf(xl - 0.5f), xe = (int)ceilf(xr - 0.5f);
    if (xs < gfx_cx0) xs = gfx_cx0;
    if (xe > gfx_cx1) xe = gfx_cx1;
    if (xe <= xs) continue;

    // Attribute values at the centre of the first pixel
    float dx = xs + 0.5f - x0, dy = yc - y0;
    float v0[NATTR];
    for (int k = 0; k < NATTR; k++) v0[k] = p0->a[k] + gx[k] * dx + gy[k] * dy;
    float iz = v0[0], uz = v0[1], vz = v0[2];
    int32_t lr = (int32_t)(v0[3] * FX), lg = (int32_t)(v0[4] * FX), lb = (int32_t)(v0[5] * FX);
    int32_t sr = (int32_t)(v0[6] * FX), sg = (int32_t)(v0[7] * FX), sb = (int32_t)(v0[8] * FX);

    uint16_t* row = gfx_fb + y * SCR_W;
    float z = 1.0f / iz;
    float u = uz * z, v = vz * z;
    int x = xs;
    while (x < xe) {
      int n = xe - x;
      if (n > 8) n = 8;
      float iz2 = iz + gx[0] * n, uz2 = uz + gx[1] * n, vz2 = vz + gx[2] * n;
      float z2 = 1.0f / iz2;
      float u2 = uz2 * z2, v2 = vz2 * z2;
      int32_t tu = (int32_t)(u * (TEX_W * FX));
      int32_t tv = (int32_t)((1.0f - v) * (TEX_H * FX));
      int32_t dtu = (int32_t)((u2 - u) * (TEX_W * FX)) / n;
      int32_t dtv = (int32_t)((v - v2) * (TEX_H * FX)) / n;
      for (int i = 0; i < n; i++, x++) {
        int tx = tu >> 16, ty = tv >> 16;
        tx = tx < 0 ? 0 : (tx > TEX_W - 1 ? TEX_W - 1 : tx);
        ty = ty < 0 ? 0 : (ty > TEX_H - 1 ? TEX_H - 1 : ty);
        uint16_t tc = sTex[ty * TEX_W + tx];
        if (usePaint && (tc & PAINT_FLAG)) {
          int r5 = tc >> 11, g6 = (tc >> 5) & 63, b5 = tc & 31;
          tc = sPaintLUT[(r5 * 154 + g6 * 150 + b5 * 58) >> 8];
        }
        int r = (((tc >> 11) & 31) * (lr >> 8)) >> 16;
        int g = (((tc >> 5) & 63) * (lg >> 8)) >> 16;
        int bl = ((tc & 31) * (lb >> 8)) >> 16;
        r += sr >> 19; g += sg >> 18; bl += sb >> 19;
        if (r > 31) r = 31;
        if (g > 63) g = 63;
        if (bl > 31) bl = 31;
        if (r < 0) r = 0;
        if (g < 0) g = 0;
        if (bl < 0) bl = 0;
        row[x] = (uint16_t)((r << 11) | (g << 5) | bl);
        tu += dtu; tv += dtv;
        lr += dlr; lg += dlg; lb += dlb; sr += dsr; sg += dsg; sb += dsb;
      }
      iz = iz2; uz = uz2; vz = vz2; u = u2; v = v2;
    }
  }
}

// ═══════════════════════════════════════════════════════════════
//  MESH: transform, light, sort, draw
// ═══════════════════════════════════════════════════════════════
struct Mat3 { float m[9]; };

static Mat3 makeRotation(float yaw, float pitch, float roll) {
  // R = Rx(pitch) * Ry(yaw) * Rz(roll). Camera space: x right, y up, z away.
  float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
  float cr = cosf(roll), sr = sinf(roll);
  // Rz(roll): leans the roof towards +x for roll > 0
  float a[9] = { cr, sr, 0, -sr, cr, 0, 0, 0, 1 };
  // Ry(yaw): nose (+z) towards +x for yaw > 0
  float b[9] = { cy, 0, sy, 0, 1, 0, -sy, 0, cy };
  // Rx(pitch): camera looks down -> far points move up the screen
  float c[9] = { 1, 0, 0, 0, cp, sp, 0, -sp, cp };
  float ba[9], r[9];
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) {
      ba[i * 3 + j] = b[i * 3 + 0] * a[0 * 3 + j] + b[i * 3 + 1] * a[1 * 3 + j] + b[i * 3 + 2] * a[2 * 3 + j];
    }
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) {
      r[i * 3 + j] = c[i * 3 + 0] * ba[0 * 3 + j] + c[i * 3 + 1] * ba[1 * 3 + j] + c[i * 3 + 2] * ba[2 * 3 + j];
    }
  Mat3 out;
  memcpy(out.m, r, sizeof(r));
  return out;
}

static inline void mul(const Mat3& M, float x, float y, float z, float& ox, float& oy, float& oz) {
  ox = M.m[0] * x + M.m[1] * y + M.m[2] * z;
  oy = M.m[3] * x + M.m[4] * y + M.m[5] * z;
  oz = M.m[6] * x + M.m[7] * y + M.m[8] * z;
}

// Project a model-space point (relative to the model centre) to the screen
static inline bool projectPt(const CarRender& cr, const Mat3& M, float x, float y, float z,
                             float& sx, float& sy) {
  float px, py, pz;
  mul(M, x, y, z - MODEL_CZ, px, py, pz);
  float d = cr.camDist + pz;
  if (d < 0.2f) return false;
  float f = cr.pxPerUnit * cr.camDist / d;
  sx = cr.ax + px * f;
  sy = cr.ay - py * f;
  return true;
}

static void drawShadow(const CarRender& cr, const Mat3& M) {
  static const float RINGS[2][3] = { { 1.75f, 3.75f, 6 }, { 1.35f, 3.25f, 12 } };
  for (int r = 0; r < 2; r++) {
    float hx = RINGS[r][0], hz = RINGS[r][1];
    // 8-point footprint (rounded rectangle) on the ground plane y=0
    const float k = 0.72f;
    float pts[8][2] = { { -hx * k, -hz }, { hx * k, -hz }, { hx, -hz * k }, { hx, hz * k },
                        { hx * k, hz }, { -hx * k, hz }, { -hx, hz * k }, { -hx, -hz * k } };
    float xy[16];
    bool ok = true;
    for (int i = 0; i < 8; i++)
      ok &= projectPt(cr, M, pts[i][0], 0.0f, pts[i][1] + MODEL_CZ, xy[2 * i], xy[2 * i + 1]);
    if (ok) gfx_fillPoly(xy, 8, COL_BLACK, GFX_BLEND, (int)(RINGS[r][2] * (1.0f - cr.fog)));
  }
}

void renderCar(const CarRender& cr) {
  if (!sTex || cr.pxPerUnit <= 0) return;
  Mat3 M = makeRotation(cr.yaw, cr.pitch, cr.roll);
  if (cr.shadow) drawShadow(cr, M);

  // ---- lighting setup (camera space) ----
  float Lx, Ly, Lz, lc[3], am[3];
  if (cr.tunnelLight) {
    // ceiling lamps just passed overhead, slightly behind the car
    Lx = 0.0f; Ly = 0.80f; Lz = -0.60f;
    lc[0] = 0.95f; lc[1] = 0.88f; lc[2] = 0.72f;
    am[0] = 0.34f; am[1] = 0.34f; am[2] = 0.38f;
  } else {
    Lx = pal.sunDirX; Ly = pal.sunDirY; Lz = pal.sunDirZ;
    lc[0] = pal.lightR; lc[1] = pal.lightG; lc[2] = pal.lightB;
    am[0] = pal.ambR; am[1] = pal.ambG; am[2] = pal.ambB;
  }
  float ls = cr.lightScale;
  float keep = 1.0f - cr.fog;
  float fogR = ((cr.fogColor >> 11) & 31) * (255.0f / 31.0f) * cr.fog;
  float fogG = ((cr.fogColor >> 5) & 63) * (255.0f / 63.0f) * cr.fog;
  float fogB = (cr.fogColor & 31) * (255.0f / 31.0f) * cr.fog;
  // Sky colour reflected at grazing angles (clear-coat look)
  float envR = ((pal.skyMid >> 11) & 31) * (255.0f / 31.0f);
  float envG = ((pal.skyMid >> 5) & 63) * (255.0f / 63.0f);
  float envB = (pal.skyMid & 31) * (255.0f / 31.0f);
  float envK = cr.tunnelLight ? 0.10f : 0.30f;

  // ---- 1. transform every vertex (positions only) ----
  for (int i = 0; i < car2_vert_count; i++) {
    const Car2Vertex& v = car2_verts[i];
    float px, py, pz;
    mul(M, v.x, v.y, v.z - MODEL_CZ, px, py, pz);
    float d = cr.camDist + pz;
    VOut& o = sV[i];
    o.depth = d;
    o.lit = 0;
    if (d < 0.2f) { o.iz = 0; continue; }
    float f = cr.pxPerUnit * cr.camDist / d;
    o.px = px; o.py = py;
    o.x = cr.ax + px * f;
    o.y = cr.ay - py * f;
    o.iz = 1.0f / d;
  }

  // ---- 2. back-face cull + depth sort (far to near) ----
  int nvis = 0;
  for (int t = 0; t < car2_tri_count; t++) {
    VOut& A = sV[car2_indices[t * 3]];
    VOut& B = sV[car2_indices[t * 3 + 1]];
    VOut& C = sV[car2_indices[t * 3 + 2]];
    if (A.iz <= 0 || B.iz <= 0 || C.iz <= 0) continue;
    float cross = (B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x);
    if (cross <= 0) continue;
    if (fmaxF(A.y, fmaxF(B.y, C.y)) < gfx_cy0 || fminF(A.y, fminF(B.y, C.y)) >= gfx_cy1) continue;
    if (fmaxF(A.x, fmaxF(B.x, C.x)) < 0 || fminF(A.x, fminF(B.x, C.x)) >= SCR_W) continue;
    A.lit = B.lit = C.lit = 1;
    float dmax = fmaxF(A.depth, fmaxF(B.depth, C.depth));
    sDepth[t] = A.depth + B.depth + C.depth + dmax;   // bias towards the far corner
    int j = nvis++;
    while (j > 0 && sDepth[sOrder[j - 1]] < sDepth[t]) { sOrder[j] = sOrder[j - 1]; j--; }
    sOrder[j] = (uint16_t)t;
  }

  // ---- 3. light only the vertices that will be drawn ----
  for (int i = 0; i < car2_vert_count; i++) {
    VOut& o = sV[i];
    if (!o.lit) continue;
    float px = o.px, py = o.py, d = o.depth;
    float nx, ny, nz;
    mul(M, car2_normals[i * 3] * (1.0f / 127), car2_normals[i * 3 + 1] * (1.0f / 127),
        car2_normals[i * 3 + 2] * (1.0f / 127), nx, ny, nz);
    float diff = nx * Lx + ny * Ly + nz * Lz;
    if (diff < 0) diff = 0;
    // view vector: from the vertex towards the eye at the origin
    float vl = 1.0f / sqrtf(px * px + py * py + d * d);
    float Vx = -px * vl, Vy = -py * vl, Vz = -d * vl;
    float hx = Lx + Vx, hy = Ly + Vy, hz = Lz + Vz;
    float hl2 = hx * hx + hy * hy + hz * hz;
    float spec = 0;
    if (hl2 > 1e-8f) {
      float nh = (nx * hx + ny * hy + nz * hz) / sqrtf(hl2);
      if (nh > 0) {   // ~nh^20
        float s2 = nh * nh, s4 = s2 * s2, s8 = s4 * s4, s16 = s8 * s8;
        spec = s16 * s4;
      }
    }
    float nv = nx * Vx + ny * Vy + nz * Vz;
    float fres = 1.0f - (nv > 0 ? nv : 0);
    fres = fres * fres * fres * envK;

    float k = ls * keep;
    float mr = (am[0] + diff * lc[0]) * k, mg = (am[1] + diff * lc[1]) * k, mb = (am[2] + diff * lc[2]) * k;
    float s = spec * 230.0f * ls * keep;
    float ar = s * lc[0] + envR * fres * keep + fogR;
    float ag = s * lc[1] + envG * fres * keep + fogG;
    float ab = s * lc[2] + envB * fres * keep + fogB;
    o.lr = (int16_t)(mr * 256.0f > 32000 ? 32000 : mr * 256.0f);
    o.lg = (int16_t)(mg * 256.0f > 32000 ? 32000 : mg * 256.0f);
    o.lb = (int16_t)(mb * 256.0f > 32000 ? 32000 : mb * 256.0f);
    o.sr = (int16_t)(ar > 255 ? 255 : ar);
    o.sg = (int16_t)(ag > 255 ? 255 : ag);
    o.sb = (int16_t)(ab > 255 ? 255 : ab);
  }

  bool usePaint = cr.paint != 0;
  if (usePaint) buildPaintLUT(cr.paint);

  for (int k = 0; k < nvis; k++) {
    int t = sOrder[k];
    RV rv[3];
    for (int c = 0; c < 3; c++) {
      int vi = car2_indices[t * 3 + c];
      const VOut& o = sV[vi];
      RV& r = rv[c];
      r.x = o.x; r.y = o.y;
      r.a[0] = o.iz;
      r.a[1] = car2_verts[vi].u * o.iz;
      r.a[2] = car2_verts[vi].v * o.iz;
      r.a[3] = o.lr; r.a[4] = o.lg; r.a[5] = o.lb;
      r.a[6] = o.sr; r.a[7] = o.sg; r.a[8] = o.sb;
    }
    rasterTri(&rv[0], &rv[1], &rv[2], usePaint);
  }

  // ---- tail / brake lights ----
  if (cr.lights || cr.brake) {
    float base = cr.brake ? 1.0f : 0.55f;
    for (int s = -1; s <= 1; s += 2) {
      float sx, sy;
      if (!projectPt(cr, M, 0.85f * s, 1.22f, -3.24f, sx, sy)) continue;
      // only when the rear faces the camera
      float nx, ny, nz;
      mul(M, 0, 0, -1, nx, ny, nz);
      if (nz > 0.1f) continue;
      int r = (int)(cr.pxPerUnit * (cr.brake ? 0.75f : 0.5f));
      if (r < 2) r = 2;
      int strength = (int)(30 * base * (1.0f - cr.fog * 0.6f));
      gfx_glow((int)sx, (int)sy, r, COL_RED, strength);
      gfx_glow((int)sx, (int)sy, r / 3 + 1, rgb(255, 160, 120), strength);
    }
  }
}

// ═══════════════════════════════════════════════════════════════
//  PLAYER CAR
// ═══════════════════════════════════════════════════════════════
static float sSmoothPitch = 0.0f;

void drawPlayerCar(float timeSec) {
  int segIdx = findSegIdx(position + playerZdist);
  Segment& seg = segments[segIdx];

  // Road slope over a few segments, smoothed (same as the camera sees it)
  const int SLOPE_SAMPLES = 6;
  float yStart = segments[segIdx].y;
  float yEnd = segments[(segIdx + SLOPE_SAMPLES) % TOTAL_SEGS].y;
  float slope = (yEnd - yStart) / (SEG_LEN * SLOPE_SAMPLES);
  float roadPitch = clampF(atanf(slope) * 0.6f, -0.25f, 0.25f);
  sSmoothPitch += (roadPitch - sSmoothPitch) * 0.15f;

  float spPct = speed / maxSpeed;
  // Engine/road vibration, stronger on the grass
  float bounce = sinf(timeSec * 31.0f) * 0.5f * spPct + sinf(timeSec * 53.0f) * 2.2f * offRoad * spPct;

  CarRender cr;
  cr.ax = SCR_CX + steerVisual * 6.0f;
  cr.ay = SCR_H - 13 + bounce;
  cr.pxPerUnit = 27.0f;
  cr.yaw = steerVisual * 0.20f + driftAngle * 0.35f;
  cr.pitch = 0.30f + sSmoothPitch;
  cr.roll = -steerVisual * 0.035f - seg.curve * spPct * 0.006f;
  cr.camDist = 11.0f;
  cr.paint = PLAYER_PAINT;
  cr.fog = 0;
  cr.fogColor = pal.fog;
  cr.tunnelLight = seg.tunnel != 0;
  // Tunnel: lights pass overhead every 4 segments
  float pulse = 1.0f;
  if (seg.tunnel) {
    float ph = fmodf((position + playerZdist) / (SEG_LEN * 4.0f), 1.0f);
    pulse = 0.55f + 0.45f * expf(-ph * ph * 30.0f);
  }
  cr.lightScale = seg.tunnel ? pulse : 1.0f;
  cr.lights = pal.night > 0.3f || seg.tunnel;
  cr.brake = braking;
  cr.shadow = true;

  // Headlight pool on the road ahead at night / in tunnels
  float beam = seg.tunnel ? 0.6f : clampF((pal.night - 0.2f) * 1.4f, 0, 1);
  if (beam > 0.02f) {
    int bx = (int)cr.ax, by = SCR_H - 78;
    gfx_fillEllipse(bx, by, 88, 20, pal.lampLight, GFX_ADD, (int)(5 * beam));
    gfx_fillEllipse(bx, by + 4, 64, 14, pal.lampLight, GFX_ADD, (int)(6 * beam));
    gfx_fillEllipse(bx, by + 8, 40, 9, pal.lampLight, GFX_ADD, (int)(7 * beam));
  }

  renderCar(cr);
}

// ═══════════════════════════════════════════════════════════════
//  START SCREEN
// ═══════════════════════════════════════════════════════════════
void drawStartScreen(float timeSec) {
  drawSky(timeSec * 12.0f, timeSec);
  // Showroom floor: dark gradient with a soft spotlight
  for (int y = SCR_CY; y < SCR_H; y++) {
    float t = (float)(y - SCR_CY) / (SCR_H - SCR_CY);
    gfx_ditherRow(y, 0, SCR_W, lerpCol(pal.skylineNear, COL_BLACK, 0.3f), rgb(8, 8, 14), t);
  }
  gfx_fillEllipse(SCR_CX, 186, 120, 26, pal.sunGlow, GFX_ADD, 3);
  gfx_fillEllipse(SCR_CX, 186, 92, 19, rgb(40, 40, 52), GFX_SOLID, 32);
  gfx_fillEllipse(SCR_CX, 186, 88, 17, rgb(64, 64, 80), GFX_SOLID, 32);

  CarRender cr;
  cr.ax = SCR_CX; cr.ay = 188;
  cr.pxPerUnit = 17.0f;
  cr.yaw = timeSec * 1.1f + 2.4f;
  cr.pitch = 0.32f;
  cr.roll = 0;
  cr.camDist = 14.0f;
  cr.paint = PLAYER_PAINT;
  cr.fog = 0; cr.fogColor = 0;
  cr.lightScale = 1.0f;
  cr.tunnelLight = false;
  cr.lights = true;
  cr.brake = false;
  cr.shadow = true;
  renderCar(cr);

  // Title
  gfx_fillRectMode(0, 14, SCR_W, 58, COL_BLACK, GFX_BLEND, 14);
  gfx_fillRect(0, 16, SCR_W, 2, COL_RED);
  gfx_fillRect(0, 69, SCR_W, 2, COL_RED);
  gfx_textCentered(SCR_CX, 22, "ARCADE 3D", COL_WHITE, 4, rgb(150, 0, 0));
  gfx_textCentered(SCR_CX, 56, "ESP32-S3 PSEUDO-3D RACER", COL_YELLOW, 1, COL_BLACK);

  if (((int)(timeSec * 2.0f)) & 1)
    gfx_textCentered(SCR_CX, 218, "STEER OR PRESS START", COL_WHITE, 1, COL_BLACK);
}

// ═══════════════════════════════════════════════════════════════
//  CRASH OVERLAY
// ═══════════════════════════════════════════════════════════════
void drawCrashMessage(float since) {
  if (since < 0.3f) {   // impact flash
    int a = (int)((1.0f - since / 0.3f) * 18.0f);
    gfx_fillRectMode(0, 0, SCR_W, SCR_H, COL_WHITE, GFX_BLEND, a);
  }
  gfx_fillRectMode(0, SCR_CY - 22, SCR_W, 44, COL_BLACK, GFX_BLEND, 18);
  gfx_fillRect(0, SCR_CY - 22, SCR_W, 2, COL_RED);
  gfx_fillRect(0, SCR_CY + 20, SCR_W, 2, COL_RED);
  gfx_textCentered(SCR_CX, SCR_CY - 10, "CRASH!", COL_RED, 3, COL_BLACK);
}
