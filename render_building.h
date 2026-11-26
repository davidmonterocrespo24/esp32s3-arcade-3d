/*
  ═══════════════════════════════════════════════════════════════
  BUILDING RENDERING (every BuildingType: city blocks, villas,
  cliffs, retaining walls)
  ═══════════════════════════════════════════════════════════════
*/

#ifndef RENDER_BUILDING_H
#define RENDER_BUILDING_H

#include <stdint.h>
#include "render_road.h"

// One segment-long slice of a building beside the road.
//   pn / pf    : projected road centre at the near / far end of the segment
//   heightUnits: Segment::build[side] (height / BUILDING_H_UNIT)
//   style      : Segment::style[side] (BuildingType | variant << 4)
//   showFront  : first slice of this building -> draw its front facade
//   fog        : 0..1 haze towards pal.fog
void drawBuilding(const ProjPt& pn, const ProjPt& pf, int heightUnits, uint8_t style,
                  int sIdx, bool isLeft, bool showFront, float fog);

// ═══════════════════════════════════════════════════════════════
//  LOOK TUNABLES (renderer only; physics never reads them)
// ═══════════════════════════════════════════════════════════════
// Screen exaggeration of building heights. In the shared units a floor
// (BUILDING_FLOOR_H) is ~1/17 of a street lamp, which made the city a toy
// town (lamps and trees taller than 9-floor blocks); at 3x a floor is about
// a third of a lamp and the streets read as a downtown canyon. The circuit
// generator keeps placing heights in floors / units as before.
#define BLD_V_SCALE      3.0f
// Building depth away from the road, x BUILDING_W (front facades, roofs)
#define BLD_DEPTH_SCALE  1.6f
// Pitched villa roofs: ridge height in floors
#define BLD_RIDGE_FLOORS 0.55f

// Called once per frame by drawRoad() before any building (resets the
// per-frame palette caches).
void buildings_beginFrame();

// ═══════════════════════════════════════════════════════════════
//  TEXTURED VERTICAL WALL (shared with the road pass: tunnel walls)
//  A wall slice between two screen columns xa..xb whose ground and top
//  edges are straight lines; u along the wall is perspective-correct
//  (u/z and 1/z interpolated), v linear per column. Bands from the top:
//  cap (capV texels), body (repeats; one floor per period, window cells
//  re-coloured when `windows`), base (baseV texels at the bottom).
// ═══════════════════════════════════════════════════════════════
struct WallTex;
struct WallJob {
  float xa, xb;              // screen x of the two ends
  float gya, gyb;            // ground line y at the ends
  float tya, tyb;            // top line y at the ends
  float iza, izb;            // 1 / camera depth at the ends
  float uza, uzb;            // u / z (u: level-0 texels along the wall)
  const WallTex* tex;
  float vTot;                // texels from the top edge to the ground
  float capV, baseV;         // texels of the cap band (top) and base band (bottom)
  int   floors;              // floors (windowed walls): body periods + base
  int   dayState;            // -1 hashed window looks, -2 graded by height (curtain wall)
  bool  windows;             // re-colour window slots per cell / floor
  float jag;                 // jagged top amplitude in texels (0 = straight)
  const uint16_t* pal;       // cap + body palette (16 entries)
  const uint16_t* palBase;   // base band palette
  const uint16_t (*day)[3];  // window day looks (TEX_WIN_STATES)
  const uint16_t (*lit)[3];  // window lights (4)
  uint32_t seed;             // window pattern seed
  int   litPct, night100;
  uint16_t farCol;           // flat LOD colour (< ~1.3 px per floor)
};
void drawWallColumns(const WallJob& J);

#endif // RENDER_BUILDING_H
