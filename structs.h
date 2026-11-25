/*
  ═══════════════════════════════════════════════════════════════
  GAME DATA STRUCTURES
  ═══════════════════════════════════════════════════════════════
*/

#ifndef STRUCTS_H
#define STRUCTS_H

#include <stdint.h>

// ═══════════════════════════════════════════════════════════════
//  TRACK SEGMENT — packed to 16 bytes (was 32 with int/bool padding).
//  1000 segments = 16 KB of internal SRAM.
// ═══════════════════════════════════════════════════════════════
struct Segment {
  float    curve;           // Segment curvature
  float    y;               // Elevation at the FAR end of the segment
  int8_t   spriteType;      // Scenery sprite (-1 = none)
  int8_t   spriteOff16;     // Lateral sprite offset * 16 (road half-widths)
  uint8_t  tunnel;          // 1 = inside tunnel
  uint8_t  lamp;            // street lamp: 0 none, 1 left, 2 right
  uint8_t  buildL, buildR;  // Building height / BUILDING_H_UNIT (0 = none)
  uint8_t  styleL, styleR;  // bits 0-2 facade palette, bits 3-5 window style

  float spriteOffset() const { return spriteOff16 * (1.0f / 16.0f); }
};

// ═══════════════════════════════════════════════════════════════
//  TRAFFIC CAR
// ═══════════════════════════════════════════════════════════════
struct TrafficCar {
  float    offset;          // Lateral offset (-1 to 1)
  float    targetOffset;    // Lane it is drifting towards
  float    z;               // Position on track
  float    speed;           // Car speed
  uint16_t color;           // Paint colour (RGB565)
};

#endif // STRUCTS_H
