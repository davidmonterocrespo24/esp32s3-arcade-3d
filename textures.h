/*
  ═══════════════════════════════════════════════════════════════
  PROCEDURAL TEXTURES ("invented" at boot from seeded hash noise)

  Every texture is 8-bit indexed into 16 SEMANTIC slots (TexSlot):
  the texels say "brick", "mortar", "window pane", "neon"..., and the
  renderer turns slots into RGB565 per face / per slice / per row
  (sun shading, fog, time of day and night lighting baked into a
  16-entry palette), so the per-pixel work is one texel load and one
  palette load. Sizes are powers of two; every texture has 4 mip
  levels generated from the same analytic design (thin details drop
  out, windows survive as dots), so distant surfaces do not shimmer.

  WallTex: column-major (a screen column walks a texture column) for
           facades, cliffs, retaining walls, tunnel walls, roof tiles.
  RowTex : row-major (a screen row walks a texture row) for asphalt,
           pavement + kerb and the ground materials.

  Memory: one block of ~23 KB, internal SRAM first (PSRAM fallback,
  placement logged); the START / FINISH banners go to PSRAM.
  ═══════════════════════════════════════════════════════════════
*/

#ifndef TEXTURES_H
#define TEXTURES_H

#include <stdint.h>

// ═══════════════════════════════════════════════════════════════
//  SEMANTIC PALETTE SLOTS (shared by every texture)
// ═══════════════════════════════════════════════════════════════
enum TexSlot : uint8_t {
  TS_DARK = 0,   // deep shadow, recess, grout, crack
  TS_SHADE,      // shaded material
  TS_BASE,       // base material
  TS_LIGHT,      // lit material / highlight
  TS_ALT,        // secondary material (mortar, spandrel panel, patch)
  TS_ALT2,       // secondary material, darker
  TS_TRIM,       // trim: sills, cornices, coping (light)
  TS_TRIMD,      // trim shadow
  TS_ACC,        // accent: awning, shutters, sign board, stripe
  TS_ACC2,       // accent 2: awning stripe, vegetation
  TS_FRAME,      // window frame / mullion
  TS_WINA,       // window pane (main)        } replaced per window:
  TS_WINB,       // window pane (upper/light) } day reflection or
  TS_WINC,       // window pane (lower/blind) } night lit / dark
  TS_EMITA,      // emissive A: light strip, shop interior (lit at night)
  TS_EMITB,      // emissive B: sign letters, neon (lit at night)
  TS_COUNT };

#define TEX_LEVELS 4

// Column-major wall texture. Rows from the top: CAP module (top of the
// wall: parapet, cornice, coping), BODY module (repeats: one floor of a
// building), BASE module (ground floor / footing). Level l holds
// (1 << wLog2) >> l columns of rows >> l texels each.
struct WallTex {
  uint8_t  wLog2;                 // columns at level 0 = 1 << wLog2
  uint8_t  cap, body, base;       // module rows at level 0 (multiples of 8, body a power of two)
  uint8_t  rows;                  // cap + body + base
  uint8_t  winShift;              // log2 of the window width in texels (night lighting cell)
  const uint8_t* lev[TEX_LEVELS]; // column-major texels
  const uint8_t* avg[TEX_LEVELS]; // one column per level: row-wise majority (grazing views)
};

// Row-major ground texture, level l is (W >> l) x (H >> l)
struct RowTex {
  uint8_t wLog2, hLog2;
  const uint8_t* lev[TEX_LEVELS];
};

enum WallTexId : uint8_t {
  WT_OFFICE = 0,    // concrete grid, recessed punched windows
  WT_RIBBON,        // office with ribbon windows and spandrel bands
  WT_GLASS,         // curtain wall, mullions, sky reflection streaks
  WT_BRICK,         // running bond, mortar, sills and lintels
  WT_STONE,         // ashlar, cornices, tall windows with pediments
  WT_MODERN,        // dark panels, narrow windows, vertical light strips
  WT_SHOPS,         // shopfronts + awnings + signs, flats above
  WT_VILLA,         // stucco, small shuttered windows
  WT_CLIFF,         // layered rock (no floors)
  WT_WALL,          // concrete retaining-wall panels
  WT_TUNNEL,        // tunnel lining: tiles, stripe, skirting
  WT_ROOF,          // terracotta for villa roofs: colour variants only (the
                    // roof is drawn as shaded tile courses, not sampled)
  WT_COUNT };

enum RowTexId : uint8_t {
  RT_ASPHALT = 0,   // aggregate, cracks, patches, tar seams, oil
  RT_PAVEMENT,      // kerb (u 0..4) + sidewalk slabs, spans |x| 1.0 .. 1.5
  RT_GRASS, RT_SAND, RT_WATER, RT_ROCK, RT_SNOW, RT_DIRT,
  RT_COUNT };

// Texel scales (level 0)
#define TEX_U_PER_SEG      16      // wall texels per segment along the road
#define TEX_V_PER_FLOOR    16      // wall texels per building floor
#define TEX_ASPHALT_U      128.0f  // asphalt texels per road half-width
#define TEX_ASPHALT_V      (32.0f / (4.0f * 200.0f))  // asphalt rows per world unit (32 rows = 4 segments)
#define TEX_PAVE_U         64.0f   // pavement texels per road half-width (u = 0 at the road edge)
#define TEX_PAVE_V         (16.0f / (2.0f * 200.0f))  // 16 rows = 2 segments
#define TEX_GROUND_U       32.0f   // ground material texels per road half-width
#define TEX_GROUND_V       (16.0f / (2.0f * 200.0f))

// Synthesise every texture once (idempotent). Returns false only when no
// memory at all could be found (the renderers then fall back to flat fills).
bool textures_init();
bool textures_ready();

const WallTex& tex_wall(int id);
const RowTex&  tex_row(int id);

// START (0) / FINISH (1) banner: row-major, TEX_BANNER_W x TEX_BANNER_H,
// nullptr when not available. Slots: TS_DARK background, TS_LIGHT /
// TS_SHADE chequer, TS_EMITB letters.
#define TEX_BANNER_W 128
#define TEX_BANNER_H 16
const uint8_t* tex_banner(int which);

// ═══════════════════════════════════════════════════════════════
//  PALETTES
// ═══════════════════════════════════════════════════════════════
// Albedo (RGB888) per slot of one colour variant. Emissive slots hold the
// daytime (unlit) colour here; their night colour is in `glow`.
struct TexColors {
  uint8_t rgb[TS_COUNT][3];
  uint8_t glow[2][3];             // night colour of TS_EMITA / TS_EMITB
};

#define TEX_VARIANTS 4
// Colour variant v (0..3) of a wall texture; the theme picks rock tints.
const TexColors& tex_colors(int wallTexId, int variant);

// Faces: sun shading factor of the palette light; TF_TUNNEL is lit by the
// tunnel lamps instead (the same at any time of day)
enum TexFace : uint8_t { TF_LIT = 0, TF_SHADE, TF_FRONT, TF_ROOF, TF_TUNNEL, TF_COUNT };

// Once per frame, before any palette is built: caches the lighting of
// the current palette `pal` (colors.h) per face, plus the window sets.
void tex_beginFrame();

// 16 RGB565 entries: material slots shaded for the face, emissive slots
// blended towards their glow by the night, all fogged (fog32 0..32
// towards fogc). Window slots get the "unlit" day look (callers patch
// them per window with tex_windowSet).
void tex_buildPalette(uint16_t out[TS_COUNT], const TexColors& c, int face, uint16_t fogc, int fog32);

// Night uplight on the lower floors (street lamps, shop spill): the same
// palette with a warm light added to the material slots.
void tex_buildUplitPalette(uint16_t out[TS_COUNT], const TexColors& c, int face, uint16_t fogc,
                           int fog32, float amount);

// Window looks (TS_WINA, TS_WINB, TS_WINC per state), fogged:
//  - day looks: 8 states built from the CURRENT sky (reflections, blinds,
//    curtains, dark interiors; they darken with the sky, so an unlit window
//    at night reflects the night sky). curtain = true: a gradient from the
//    street reflection (state 0) to the sky (state 7) for glass towers.
//    tint: glass tint (RGB888), face: mild per-face shading.
//  - lit looks: 4 interior lights (warm, white, cool, TV blue), not shaded.
#define TEX_WIN_STATES 8
void tex_windowSets(uint16_t out[TEX_WIN_STATES][3], int face, const uint8_t tint[3], bool curtain,
                    uint16_t fogc, int fog32);
void tex_litSets(uint16_t out[4][3], uint16_t fogc, int fog32);

// The 3 colours of window cell (col, floor) of a building (seed). A window
// is lit when it is one of the litPct % lit ones AND the night (0..100) has
// passed its own threshold, so the lights come on one by one at dusk.
// dayState < 0: hashed day look, else that state (curtain wall gradient).
static inline const uint16_t* tex_pickWindow(const uint16_t day[TEX_WIN_STATES][3],
                                             const uint16_t lit[4][3], uint32_t seed, int col,
                                             int floor, int litPct, int night100, int dayState) {
  uint32_t h = seed ^ ((uint32_t)col * 0x9E3779B1u) ^ ((uint32_t)floor * 0x85EBCA77u);
  h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 13;
  // bits: 0..6 lit roll (of 128), 7..12 switch-on threshold, 13..15 light
  // colour (warm lights dominate: warm 4/8, white 2/8, cool 1/8, TV 1/8),
  // 16..18 day look
  static const uint8_t LIT_PICK[8] = { 0, 0, 1, 0, 2, 1, 0, 3 };
  if ((int)(h & 127u) * 100 < litPct * 128 && night100 > 12 + (int)((h >> 7) & 63u))
    return lit[LIT_PICK[(h >> 13) & 7]];
  return day[dayState >= 0 ? (dayState & 7) : (int)((h >> 16) & 7)];
}

// Row-texture palette from a base colour (RGB565, no fog): per slot the
// base times the texture's relative factors, then fogged.
void tex_rowPalette(uint16_t out[TS_COUNT], int rowTexId, uint16_t base, uint16_t fogc, int fog32);

// Asphalt detail slots that a segment may hide (cracks, patches, seams,
// oil): a mask of TS_* bits; slots not in `show` take the base colour.
void tex_asphaltPalette(uint16_t out[TS_COUNT], uint16_t base, uint32_t showMask, uint16_t fogc, int fog32);

// Pavement palette: kerb slots from `kerb`, slab slots from `slab`.
void tex_pavementPalette(uint16_t out[TS_COUNT], uint16_t slab, uint16_t kerb, uint16_t fogc, int fog32);

#endif // TEXTURES_H
