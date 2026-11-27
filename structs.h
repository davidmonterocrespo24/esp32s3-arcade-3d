/*
  ═══════════════════════════════════════════════════════════════
  GAME DATA STRUCTURES — the track segment (shared contract)

  Frozen interface: every module reads segments through these fields.
  Coordinates: x is lateral in road half-widths (road edge at |x| = 1),
  z runs along the track in world units (SEG_LEN per segment).
  ═══════════════════════════════════════════════════════════════
*/

#ifndef STRUCTS_H
#define STRUCTS_H

#include <stdint.h>

#define MAX_SEGS 2400          // capacity; each circuit uses segCount <= MAX_SEGS

// What lies beyond the road edge on each side (rendered by render_road,
// slows the car in physics; layout constants in config.h). MAT_SAND is a
// beach: sand next to the road, then the sea further out. MAT_WATER is water
// just past a narrow sea wall at the road edge (put a guard rail there).
// MAT_SIDEWALK is kerb + sidewalk up to the building line, street beyond.
enum GroundMat : uint8_t {
  MAT_GRASS = 0, MAT_SIDEWALK, MAT_SAND, MAT_WATER, MAT_ROCK, MAT_SNOW, MAT_DIRT, MAT_COUNT };

// Segment::style = type | (variant << 4), variant 0..15
enum BuildingType : uint8_t {
  BLD_NONE = 0,
  BLD_OFFICE,   // concrete office block, regular window grid
  BLD_GLASS,    // glass curtain-wall tower: sky reflection, mullions
  BLD_BRICK,    // brick apartments, punched windows, ledges
  BLD_STONE,    // sandstone classic building, cornices, tall windows
  BLD_MODERN,   // dark slate tower, vertical light strips / neon at night
  BLD_SHOPS,    // 2-4 floors, shopfronts with awnings / signs at street level
  BLD_VILLA,    // coast: white stucco villa, small windows, terracotta roof
  BLD_CLIFF,    // natural rock wall (cuttings, mountain side), no windows
  BLD_WALL,     // concrete retaining wall / sound barrier
  BLD_TYPE_COUNT };

#define SEGF_TUNNEL    0x01
#define SEGF_LAMP_L    0x02    // street lamp on the left at this segment
#define SEGF_LAMP_R    0x04
#define SEGF_CROSSWALK 0x08    // zebra crossing across the road
#define SEGF_START     0x10    // start/finish line at the NEAR edge of this segment (segment 0)
#define SEGF_RAIL_L    0x20    // guard rail along the left road edge (solid for physics)
#define SEGF_RAIL_R    0x40
#define SEGF_SPARE     0x80    // free; circuits.cpp uses it as scratch while generating (cleared after)

struct Segment {               // 20 bytes
  float   curve;
  float   y;                   // elevation at the FAR end of the segment
  uint8_t flags;               // SEGF_*
  uint8_t ground;              // left material | (right material << 4)
  uint8_t build[2];            // [0] left, [1] right: height / BUILDING_H_UNIT, 0 = none
  uint8_t style[2];            // BuildingType | variant << 4
  int8_t  spr[2];              // scenery sprite type per side, -1 = none
  uint8_t sprOff[2];           // distance of the sprite from the road centre, road half-widths * 16
  uint8_t deco;                // reserved (unused for now; every placeable prop is a sprite)
  uint8_t pad;
  bool  isTunnel() const { return flags & SEGF_TUNNEL; }
  int   mat(int side) const  { return side ? (ground >> 4) : (ground & 15); }
  float sprX(int side) const { return (side ? 1.0f : -1.0f) * sprOff[side] * (1.0f / 16.0f); }
};

static_assert(sizeof(Segment) == 20, "Segment must stay 20 bytes");

// Packing helpers (side: 0 = left, 1 = right)
static inline uint8_t groundPack(int left, int right) { return (uint8_t)((left & 15) | ((right & 15) << 4)); }
static inline uint8_t bldStyle(int type, int variant) { return (uint8_t)((type & 15) | ((variant & 15) << 4)); }
static inline int     bldType(uint8_t style)    { return style & 15; }
static inline int     bldVariant(uint8_t style) { return style >> 4; }

#endif // STRUCTS_H
