/*
  ═══════════════════════════════════════════════════════════════
  GAME DATA STRUCTURES
  ═══════════════════════════════════════════════════════════════
*/

#ifndef STRUCTS_H
#define STRUCTS_H

#include <Arduino.h>

// ═══════════════════════════════════════════════════════════════
//  TRACK SEGMENT STRUCTURE
// ═══════════════════════════════════════════════════════════════
struct Segment {
  float curve;              // Segment curvature
  float y;                  // Height (elevation)
  int8_t spriteType;        // Sprite type (-1 = none)
  float  spriteOffset;      // Lateral sprite offset

  // -- 3D POLYGONAL PROPERTIES --
  bool   tunnel;            // true = inside tunnel
  int    buildL, buildR;    // Left/Right building height (0 = no building)
  uint16_t colorL, colorR;  // Building facade color
};

// ═══════════════════════════════════════════════════════════════
//  RENDER POINT
// ═══════════════════════════════════════════════════════════════
struct RenderPt {
  int16_t x, y, w;          // Screen position and width
  float   scale;            // Scale factor
};

// ═══════════════════════════════════════════════════════════════
//  TRAFFIC CAR
// ═══════════════════════════════════════════════════════════════
struct TrafficCar {
  float    offset;          // Lateral offset (-1 to 1)
  float    z;               // Position on track
  float    speed;           // Car speed
  uint16_t color;           // Car color
};

#endif // STRUCTS_H
