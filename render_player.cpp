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

  Feature phase (rivals):
  - The texture is stored as 8-bit palette indices (16 KB instead of
    32 KB of internal RAM). After the paint flag it has 234 distinct
    values, so the palette is exact: the car looks identical.
  - Mesh arrays are copied to internal RAM only when plenty is left
    (otherwise they are read from flash, as before).
  - Triangles are depth-sorted with a 256-bucket counting sort (O(n))
    instead of an insertion sort (O(n^2) per car, x6 cars a frame).
  - CAR_LOD_MID: no specular / sky reflection, one shadow ring.
  - Turbo flame from the exhausts, for the player and the rivals.
  ═══════════════════════════════════════════════════════════════
*/

#pragma GCC optimize("O2")

#include <Arduino.h>      // PROGMEM for the texture header
#include "render_player.h"
#include "gfx.h"
#include "colors.h"
#include "config.h"
#include "physics.h"
#include "track.h"
#include "utils.h"
#include "car2_mesh.h"
#include "car2_texture.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#if PLATFORM_ESP32
#include "esp_heap_caps.h"
#endif

#define TEX_W CAR2_TEX_W
#define TEX_H CAR2_TEX_H
#define PAINT_FLAG 0x0020    // green LSB of a texel marks car paint

static uint8_t*  sTexI = nullptr;        // TEX_W*TEX_H palette indices
static uint16_t  sTexPal[256];           // RGB565 (+ paint flag), internal .bss
static const float MODEL_CZ = -0.03f;    // model centre along z (front is +z)

// Mesh arrays: flash by default, an internal-RAM copy when the budget allows
static const Car2Vertex* sVerts   = car2_verts;
static const int8_t*     sNormals = car2_normals;
static const uint16_t*   sIdx     = car2_indices;

// Per-vertex work arrays (internal .bss, ~16 KB)
struct VOut { float x, y, iz, depth, px, py; int16_t lr, lg, lb, sr, sg, sb; uint8_t lit; };
static VOut sV[car2_vert_count];
static uint16_t sOrder[car2_tri_count];   // draw order (far -> near)
static uint16_t sVis[car2_tri_count];     // visible triangles
static float    sDepth[car2_tri_count];   // their depth keys
static uint8_t  sBucket[car2_tri_count];
static uint16_t sPaintLUT[64];

// Mesh parts, each with its own transform: the front wheels steer, the body
// leans on its springs, the wheels stay upright on the road.
enum : uint8_t { PART_BODY = 0, PART_REAR_WHEELS, PART_FRONT_LEFT, PART_FRONT_RIGHT, NUM_PARTS };
static uint8_t sPart[car2_vert_count];              // part of every vertex (found at boot)
static float   sWheelX[NUM_PARTS], sWheelZ[NUM_PARTS];  // steering axis of each front wheel
static const float ROLL_CENTER_Y = 0.55f;           // the body leans about this height (axles ~0.43)

// Rasterizer (below), forward declared for the boot IRAM check. No HOT here:
// IRAM_ATTR names a unique section per use, so it goes on the definition only.
struct RV;
static void rasterTri(const RV* p0, const RV* p1, const RV* p2, bool usePaint);

static uint16_t ufFind(uint16_t* r, uint16_t a) {
  while (r[a] != a) { r[a] = r[r[a]]; a = r[a]; }
  return a;
}

// The four wheels are separate pieces of the mesh (no vertex shared with the
// body). Find them by connectivity: vertices at the same position are one
// point (the normal crease splits them) and a triangle joins its corners.
// If none is found every vertex stays PART_BODY and the car is drawn rigid.
static void findWheels() {
  uint16_t root[car2_vert_count];    // boot-time scratch on the stack (~0.9 KB)
  for (int i = 0; i < car2_vert_count; i++) root[i] = (uint16_t)i;
  for (int i = 0; i < car2_vert_count; i++) {
    const Car2Vertex& a = car2_verts[i];
    for (int j = 0; j < i; j++) {
      const Car2Vertex& b = car2_verts[j];
      if (a.x == b.x && a.y == b.y && a.z == b.z) { root[ufFind(root, i)] = ufFind(root, j); break; }
    }
  }
  for (int t = 0; t < car2_tri_count; t++) {
    uint16_t a = ufFind(root, car2_indices[t * 3]);
    root[ufFind(root, car2_indices[t * 3 + 1])] = a;
    root[ufFind(root, car2_indices[t * 3 + 2])] = a;
  }
  for (int i = 0; i < car2_vert_count; i++) root[i] = ufFind(root, i);

  int wheels = 0;
  for (int c = 0; c < car2_vert_count; c++) {
    if (root[c] != c) continue;
    int n = 0;
    float x0 = 1e9f, x1 = -1e9f, y1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
    for (int i = 0; i < car2_vert_count; i++) {
      if (root[i] != c) continue;
      const Car2Vertex& v = car2_verts[i];
      n++;
      x0 = fminF(x0, v.x); x1 = fmaxF(x1, v.x); y1 = fmaxF(y1, v.y);
      z0 = fminF(z0, v.z); z1 = fmaxF(z1, v.z);
    }
    if (n > 64 || y1 > 1.0f) continue;      // the body, or a small piece up high (mirrors)
    float cx = (x0 + x1) * 0.5f, cz = (z0 + z1) * 0.5f;
    uint8_t part = cz < 0 ? PART_REAR_WHEELS : (cx < 0 ? PART_FRONT_LEFT : PART_FRONT_RIGHT);
    for (int i = 0; i < car2_vert_count; i++)
      if (root[i] == c) sPart[i] = part;
    sWheelX[part] = cx;
    sWheelZ[part] = cz;
    wheels++;
  }
  plat_log("car: %d wheels found in the mesh\n", wheels);
}

static size_t internalFree() {
#if PLATFORM_ESP32
  return heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
#else
  return (size_t)1 << 24;
#endif
}

void initCarRenderer() {
  // ---- texture: paint-flagged texels -> 8-bit indices into an exact palette ----
  const size_t bytes = TEX_W * TEX_H;
  bool internal = true;
  sTexI = (uint8_t*)plat_alloc_internal(bytes);
  if (!sTexI) { internal = false; sTexI = (uint8_t*)plat_alloc_psram(bytes); }
  if (!sTexI) { plat_log("car: no memory for the texture\n"); return; }
  int nPal = 0, inexact = 0;
  for (int i = 0; i < TEX_W * TEX_H; i++) {
    uint16_t c = car2_texture[i];
    int r = ((c >> 11) & 31) * 255 / 31, g = ((c >> 5) & 63) * 255 / 63, b = (c & 31) * 255 / 31;
    int dg = r - g; if (dg < 0) dg = -dg;
    bool paint = (b > r + 15) && (dg < 14) && (b > 40) && (b < 150);
    uint16_t t = (uint16_t)((c & ~PAINT_FLAG) | (paint ? PAINT_FLAG : 0));
    int k = 0;
    while (k < nPal && sTexPal[k] != t) k++;
    if (k == nPal) {
      if (nPal < 256) {
        sTexPal[nPal++] = t;
      } else {
        // more than 256 values (not the case for car2.png): nearest entry
        int best = 0, bd = 1 << 30;
        for (int q = 0; q < 256; q++) {
          int dr = ((sTexPal[q] >> 11) & 31) - ((t >> 11) & 31);
          int dgg = ((sTexPal[q] >> 5) & 63) - ((t >> 5) & 63);
          int db = (sTexPal[q] & 31) - (t & 31);
          int d = dr * dr * 4 + dgg * dgg + db * db * 4;
          if ((sTexPal[q] & PAINT_FLAG) != (t & PAINT_FLAG)) d += 1 << 20;
          if (d < bd) { bd = d; best = q; }
        }
        k = best;
        inexact++;
      }
    }
    sTexI[i] = (uint8_t)k;
  }
  plat_log("car: texture %dx%d as %d-colour indices (%u B) in %s%s\n", TEX_W, TEX_H, nPal,
           (unsigned)bytes, internal ? "internal SRAM" : "PSRAM", inexact ? " (approximated)" : "");
  plat_log_iram("rasterTri", (const void*)rasterTri);
  findWheels();

  // ---- mesh: copy to internal RAM only if a comfortable reserve remains ----
  const size_t vb = sizeof(Car2Vertex) * car2_vert_count;
  const size_t nb = sizeof(int8_t) * car2_vert_count * 3;
  const size_t ib = sizeof(uint16_t) * car2_tri_count * 3;
  const size_t RESERVE = 96 * 1024;
  if (internalFree() > vb + nb + ib + RESERVE) {
    uint8_t* m = (uint8_t*)plat_alloc_internal(vb + nb + ib);
    if (m) {
      memcpy(m, car2_verts, vb);
      memcpy(m + vb, car2_normals, nb);
      memcpy(m + vb + nb, car2_indices, ib);
      sVerts = (const Car2Vertex*)m;
      sNormals = (const int8_t*)(m + vb);
      sIdx = (const uint16_t*)(m + vb + nb);
      plat_log("car: mesh copied to internal SRAM (%u B)\n", (unsigned)(vb + nb + ib));
      return;
    }
  }
  plat_log("car: mesh read from flash (internal reserve too small for a %u B copy)\n",
           (unsigned)(vb + nb + ib));
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
  const uint8_t* tex = sTexI;
  const uint16_t* pal = sTexPal;

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
        uint16_t tc = pal[tex[ty * TEX_W + tx]];
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

static Mat3 matMul(const Mat3& A, const Mat3& B) {
  Mat3 r;
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
      r.m[i * 3 + j] = A.m[i * 3] * B.m[j] + A.m[i * 3 + 1] * B.m[3 + j] + A.m[i * 3 + 2] * B.m[6 + j];
  return r;
}

static Mat3 makeRotation(float yaw, float pitch, float roll) {
  // R = Rx(pitch) * Ry(yaw) * Rz(roll). Camera space: x right, y up, z away.
  float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
  float cr = cosf(roll), sr = sinf(roll);
  // Rz(roll): leans the roof towards +x for roll > 0
  Mat3 a = { { cr, sr, 0, -sr, cr, 0, 0, 0, 1 } };
  // Ry(yaw): nose (+z) towards +x for yaw > 0
  Mat3 b = { { cy, 0, sy, 0, 1, 0, -sy, 0, cy } };
  // Rx(pitch): camera looks down -> far points move up the screen
  Mat3 c = { { 1, 0, 0, 0, cp, sp, 0, -sp, cp } };
  return matMul(c, matMul(b, a));
}

static inline void mul(const Mat3& M, float x, float y, float z, float& ox, float& oy, float& oz) {
  ox = M.m[0] * x + M.m[1] * y + M.m[2] * z;
  oy = M.m[3] * x + M.m[4] * y + M.m[5] * z;
  oz = M.m[6] * x + M.m[7] * y + M.m[8] * z;
}

// Camera-space placement of one mesh part: p = R * v + t for a model vertex v
struct PartXf { Mat3 R; float t[3]; };

// The part turned by L about the model-space pivot (px, py, pz), then the
// whole car placed by M: p = M * (L * (v - pivot) + pivot - model centre)
static PartXf partXf(const Mat3& M, const Mat3& L, float px, float py, float pz) {
  PartXf X;
  X.R = matMul(M, L);
  float lx, ly, lz;
  mul(L, px, py, pz, lx, ly, lz);
  mul(M, px - lx, py - ly, pz - lz - MODEL_CZ, X.t[0], X.t[1], X.t[2]);
  return X;
}

static PartXf bodyXf(const CarRender& cr, const Mat3& M) {
  return partXf(M, makeRotation(0, 0, cr.bodyRoll), 0, ROLL_CENTER_Y, 0);
}

// Wheels and the ground stay level; only the body leans
static PartXf chassisXf(const Mat3& M) {
  return partXf(M, makeRotation(0, 0, 0), 0, 0, 0);
}

static void partTransforms(const CarRender& cr, const Mat3& M, PartXf xf[NUM_PARTS]) {
  xf[PART_BODY] = bodyXf(cr, M);
  xf[PART_REAR_WHEELS] = chassisXf(M);
  const Mat3 S = makeRotation(cr.wheelSteer, 0, 0);
  for (int p = PART_FRONT_LEFT; p <= PART_FRONT_RIGHT; p++)
    xf[p] = partXf(M, S, sWheelX[p], 0, sWheelZ[p]);   // about the wheel's vertical axis
}

static inline void place(const PartXf& X, float x, float y, float z, float& px, float& py, float& pz) {
  mul(X.R, x, y, z, px, py, pz);
  px += X.t[0]; py += X.t[1]; pz += X.t[2];
}

// Project a model-space point of a part to the screen
static inline bool projectPt(const CarRender& cr, const PartXf& X, float x, float y, float z,
                             float& sx, float& sy) {
  float px, py, pz;
  place(X, x, y, z, px, py, pz);
  float d = cr.camDist + pz;
  if (d < 0.2f) return false;
  float f = cr.pxPerUnit * cr.camDist / d;
  sx = cr.ax + px * f;
  sy = cr.ay - py * f;
  return true;
}

static void drawShadow(const CarRender& cr, const PartXf& ground, int rings) {
  static const float RINGS[2][3] = { { 1.75f, 3.75f, 6 }, { 1.35f, 3.25f, 12 } };
  for (int r = 2 - rings; r < 2; r++) {
    float hx = RINGS[r][0], hz = RINGS[r][1];
    // 8-point footprint (rounded rectangle) on the ground plane y=0
    const float k = 0.72f;
    float pts[8][2] = { { -hx * k, -hz }, { hx * k, -hz }, { hx, -hz * k }, { hx, hz * k },
                        { hx * k, hz }, { -hx * k, hz }, { -hx, hz * k }, { -hx, -hz * k } };
    float xy[16];
    bool ok = true;
    for (int i = 0; i < 8; i++)
      ok &= projectPt(cr, ground, pts[i][0], 0.0f, pts[i][1] + MODEL_CZ, xy[2 * i], xy[2 * i + 1]);
    float a = RINGS[r][2] * (1.0f - cr.fog) * (rings == 1 ? 1.4f : 1.0f);
    if (ok) gfx_fillPoly(xy, 8, COL_BLACK, GFX_BLEND, (int)a);
  }
}

void renderCar(const CarRender& cr) {
  renderCarLod(cr, CAR_LOD_FULL);
}

void renderCarLod(const CarRender& cr, int lod) {
  if (!sTexI || cr.pxPerUnit <= 0 || cr.camDist <= 0.5f) return;
  const bool full = lod == CAR_LOD_FULL;
  Mat3 M = makeRotation(cr.yaw, cr.pitch, cr.roll);
  PartXf xf[NUM_PARTS];
  partTransforms(cr, M, xf);
  if (cr.shadow) drawShadow(cr, xf[PART_REAR_WHEELS], full ? 2 : 1);

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
  const float camD = cr.camDist, ppu = cr.pxPerUnit;
  for (int i = 0; i < car2_vert_count; i++) {
    const Car2Vertex& v = sVerts[i];
    float px, py, pz;
    place(xf[sPart[i]], v.x, v.y, v.z, px, py, pz);
    float d = camD + pz;
    VOut& o = sV[i];
    o.depth = d;
    o.lit = 0;
    if (d < 0.2f) { o.iz = 0; continue; }
    float f = ppu * camD / d;
    o.px = px; o.py = py;
    o.x = cr.ax + px * f;
    o.y = cr.ay - py * f;
    o.iz = 1.0f / d;
  }

  // ---- 2. back-face cull, clip reject, depth keys ----
  int nvis = 0;
  float kmin = 1e30f, kmax = -1e30f;
  const float maxSpan = SCR_W * 2.0f;          // oversized (near-plane) triangles are dropped
  for (int t = 0; t < car2_tri_count; t++) {
    VOut& A = sV[sIdx[t * 3]];
    VOut& B = sV[sIdx[t * 3 + 1]];
    VOut& C = sV[sIdx[t * 3 + 2]];
    if (A.iz <= 0 || B.iz <= 0 || C.iz <= 0) continue;
    float cross = (B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x);
    if (cross <= 0) continue;
    float ymin = fminF(A.y, fminF(B.y, C.y)), ymax = fmaxF(A.y, fmaxF(B.y, C.y));
    float xmin = fminF(A.x, fminF(B.x, C.x)), xmax = fmaxF(A.x, fmaxF(B.x, C.x));
    if (ymax < gfx_cy0 || ymin >= gfx_cy1 || xmax < gfx_cx0 || xmin >= gfx_cx1) continue;
    if (xmax - xmin > maxSpan || ymax - ymin > maxSpan) continue;
    A.lit = B.lit = C.lit = 1;
    float dmax = fmaxF(A.depth, fmaxF(B.depth, C.depth));
    float key = A.depth + B.depth + C.depth + dmax;   // bias towards the far corner
    sVis[nvis] = (uint16_t)t;
    sDepth[nvis] = key;
    if (key < kmin) kmin = key;
    if (key > kmax) kmax = key;
    nvis++;
  }
  if (nvis == 0) return;

  // ---- 3. counting sort far -> near (256 buckets over the key range) ----
  {
    static uint16_t cnt[257];
    memset(cnt, 0, sizeof(cnt));
    float sc = 255.0f / fmaxF(kmax - kmin, 1e-4f);
    for (int k = 0; k < nvis; k++) {
      int b = (int)((kmax - sDepth[k]) * sc);         // 0 = farthest
      b = b < 0 ? 0 : (b > 255 ? 255 : b);
      sBucket[k] = (uint8_t)b;
      cnt[b + 1]++;
    }
    for (int b = 1; b <= 256; b++) cnt[b] += cnt[b - 1];
    for (int k = 0; k < nvis; k++) sOrder[cnt[sBucket[k]]++] = sVis[k];
  }

  // ---- 4. light only the vertices that will be drawn ----
  for (int i = 0; i < car2_vert_count; i++) {
    VOut& o = sV[i];
    if (!o.lit) continue;
    float nx, ny, nz;
    mul(xf[sPart[i]].R, sNormals[i * 3] * (1.0f / 127), sNormals[i * 3 + 1] * (1.0f / 127),
        sNormals[i * 3 + 2] * (1.0f / 127), nx, ny, nz);
    float diff = nx * Lx + ny * Ly + nz * Lz;
    if (diff < 0) diff = 0;
    float k = ls * keep;
    float mr = (am[0] + diff * lc[0]) * k, mg = (am[1] + diff * lc[1]) * k, mb = (am[2] + diff * lc[2]) * k;
    float ar = fogR, ag = fogG, ab = fogB;
    if (full) {
      float px = o.px, py = o.py, d = o.depth;
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
      float s = spec * 230.0f * ls * keep;
      ar += s * lc[0] + envR * fres * keep;
      ag += s * lc[1] + envG * fres * keep;
      ab += s * lc[2] + envB * fres * keep;
    } else {
      // small car: a touch of sky colour instead of the per-vertex reflection
      ar += envR * envK * 0.25f * keep;
      ag += envG * envK * 0.25f * keep;
      ab += envB * envK * 0.25f * keep;
    }
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
      int vi = sIdx[t * 3 + c];
      const VOut& o = sV[vi];
      RV& r = rv[c];
      r.x = o.x; r.y = o.y;
      r.a[0] = o.iz;
      r.a[1] = sVerts[vi].u * o.iz;
      r.a[2] = sVerts[vi].v * o.iz;
      r.a[3] = o.lr; r.a[4] = o.lg; r.a[5] = o.lb;
      r.a[6] = o.sr; r.a[7] = o.sg; r.a[8] = o.sb;
    }
    rasterTri(&rv[0], &rv[1], &rv[2], usePaint);
  }

  // ---- tail / brake lights ----
  if (cr.lights || cr.brake) {
    float nx, ny, nz;
    mul(M, 0, 0, -1, nx, ny, nz);
    if (nz <= 0.1f) {                     // only when the rear faces the camera
      float base = cr.brake ? 1.0f : 0.55f;
      for (int s = -1; s <= 1; s += 2) {
        float sx, sy;
        if (!projectPt(cr, xf[PART_BODY], 0.85f * s, 1.22f, -3.24f, sx, sy)) continue;
        int r = (int)(cr.pxPerUnit * (cr.brake ? 0.75f : 0.5f));
        if (r < 2) r = 2;
        int strength = (int)(30 * base * (1.0f - cr.fog * 0.6f));
        gfx_glow((int)sx, (int)sy, r, COL_RED, strength);
        gfx_glow((int)sx, (int)sy, r / 3 + 1, rgb(255, 160, 120), strength);
      }
    }
  }
}

// ═══════════════════════════════════════════════════════════════
//  TURBO FLAME
// ═══════════════════════════════════════════════════════════════
void renderCarFlame(const CarRender& cr, float strength, float timeSec) {
  if (strength <= 0.02f || cr.pxPerUnit <= 0) return;
  Mat3 M = makeRotation(cr.yaw, cr.pitch, cr.roll);
  float nx, ny, nz;
  mul(M, 0, 0, -1, nx, ny, nz);
  if (nz > 0.25f) return;                 // seen from the front: exhausts hidden
  const PartXf body = bodyXf(cr, M);
  const float k = strength * (1.0f - cr.fog * 0.7f);
  for (int s = -1; s <= 1; s += 2) {
    float fl = 0.70f + 0.30f * sinf(timeSec * 61.0f + s * 2.1f) * sinf(timeSec * 23.0f + s);
    float len = (0.9f + 0.7f * fl) * strength;          // model units behind the bumper
    float x0, y0, x1, y1;
    if (!projectPt(cr, body, 0.52f * s, 0.62f, -3.30f, x0, y0)) continue;
    if (!projectPt(cr, body, 0.52f * s, 0.66f, -3.30f - len, x1, y1)) continue;
    float r0 = fmaxF(1.5f, cr.pxPerUnit * 0.42f);
    // outer plume (blue), from the tip back to the exhaust
    for (int i = 3; i >= 0; i--) {
      float t = i / 3.0f;
      int r = (int)(r0 * (1.15f - 0.45f * t) * (0.8f + 0.4f * fl));
      gfx_glow((int)lerpF(x0, x1, t), (int)lerpF(y0, y1, t), imax(2, r), rgb(60, 110, 255),
               (int)(22 * k * (1.0f - 0.45f * t)));
    }
    // hot core at the exhaust
    gfx_glow((int)x0, (int)y0, imax(1, (int)(r0 * 0.55f)), rgb(210, 235, 255), (int)(30 * k));
    // its light on the rear bumper (stays on screen for the player's car,
    // whose exhausts sit just below the bottom edge)
    float bx, by;
    if (projectPt(cr, body, 0.50f * s, 1.00f, -3.30f, bx, by))
      gfx_glow((int)bx, (int)by, imax(2, (int)(r0 * 1.1f)), rgb(70, 140, 255), (int)(16 * k * (0.8f + 0.2f * fl)));
  }
}

// ═══════════════════════════════════════════════════════════════
//  PLAYER CAR
// ═══════════════════════════════════════════════════════════════
static float sSmoothPitch = 0.0f;
static float sFlame = 0.0f;
static float sBodyRoll = 0.0f, sBodyRollVel = 0.0f;   // sprung body lean
static float sLastTime = -1.0f;

float playerViewSlope() { return sSmoothPitch; }

void drawPlayerCar(float timeSec) {
  if (segCount <= 0) return;
  int segIdx = findSegIdx(position + playerZdist);
  const Segment& seg = segments[segIdx];
  const bool tunnel = seg.isTunnel();

  // Road slope over a few segments, smoothed (same as the camera sees it)
  const int SLOPE_SAMPLES = 6;
  float yStart = seg.y;
  float yEnd = segAt(segIdx + SLOPE_SAMPLES).y;
  float slope = (yEnd - yStart) / (SEG_LEN * SLOPE_SAMPLES);
  float roadPitch = clampF(atanf(slope) * 0.6f, -0.25f, 0.25f);
  sSmoothPitch += (roadPitch - sSmoothPitch) * 0.15f;

  float spPct = speed / maxSpeed;
  // Engine/road vibration, stronger on rough ground; a jolt after a bump
  float bounce = sinf(timeSec * 31.0f) * 0.5f * spPct + sinf(timeSec * 53.0f) * 2.2f * offRoad * spPct;
  float jolt = bumpShake * bumpShake;

  CarRender cr;
  cr.ax = SCR_CX + steerVisual * PLAYER_STEER_PX + sinf(timeSec * 71.0f) * 2.5f * jolt;
  cr.ay = PLAYER_ANCHOR_Y + bounce + cosf(timeSec * 57.0f) * 1.5f * jolt;
  cr.pxPerUnit = PLAYER_PX_PER_UNIT;
  cr.yaw = steerVisual * 0.20f + driftAngle * 0.35f;
  if (crashed) {   // spin out, slowing down over the crash time
    float t = clampF(crashTime / CRASH_TIME, 0, 1);
    cr.yaw += (1.0f - (1.0f - t) * (1.0f - t)) * 2.0f * (float)PI;
  }
  cr.pitch = PLAYER_VIEW_PITCH + sSmoothPitch;
  // The body leans out of the turn on damped springs (lean grows with speed);
  // the front wheels follow the steering
  float dt = sLastTime < 0 ? 0.0f : clampF(timeSec - sLastTime, 0.0f, 0.05f);
  sLastTime = timeSec;
  float rollTarget = -(steerVisual * CAR_BODY_ROLL + seg.curve * 0.006f) * spPct;
  const float W = BODY_ROLL_FREQ;
  sBodyRollVel += ((rollTarget - sBodyRoll) * W * W - 2.0f * BODY_ROLL_DAMPING * W * sBodyRollVel) * dt;
  sBodyRoll += sBodyRollVel * dt;
  cr.roll = 0;
  cr.bodyRoll = sBodyRoll;
  cr.wheelSteer = steerVisual * CAR_WHEEL_STEER;
  cr.camDist = PLAYER_VIEW_DIST;
  cr.paint = PLAYER_PAINT;
  cr.fog = 0;
  cr.fogColor = pal.fog;
  cr.tunnelLight = tunnel;
  // Tunnel: lights pass overhead every 4 segments
  float pulse = 1.0f;
  if (tunnel) {
    float ph = fmodf((position + playerZdist) / (SEG_LEN * 4.0f), 1.0f);
    pulse = 0.55f + 0.45f * expf(-ph * ph * 30.0f);
  }
  cr.lightScale = tunnel ? pulse : 1.0f;
  cr.lights = pal.night > 0.3f || tunnel;
  cr.brake = braking;
  cr.shadow = true;

  // Headlight pool on the road ahead at night / in tunnels
  float beam = tunnel ? 0.6f : clampF((pal.night - 0.2f) * 1.4f, 0, 1);
  if (beam > 0.02f) {
    int bx = (int)cr.ax, by = SCR_H - 78;
    gfx_fillEllipse(bx, by, 88, 20, pal.lampLight, GFX_ADD, (int)(5 * beam));
    gfx_fillEllipse(bx, by + 4, 64, 14, pal.lampLight, GFX_ADD, (int)(6 * beam));
    gfx_fillEllipse(bx, by + 8, 40, 9, pal.lampLight, GFX_ADD, (int)(7 * beam));
  }

  renderCar(cr);

  // Turbo flame (fades in and out over ~0.1 s)
  float want = (boosting && !crashed) ? 1.0f : 0.0f;
  sFlame += (want - sFlame) * 0.5f;
  if (sFlame > 0.02f) renderCarFlame(cr, sFlame, timeSec);
}
