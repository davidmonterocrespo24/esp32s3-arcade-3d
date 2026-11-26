/*
  ═══════════════════════════════════════════════════════════════
  SHARED GAME CONSTANTS
  Only constants that more than one module depends on live here
  (screen, world scale, building/limit geometry). Module tunables
  live in the module's own header: physics in physics.h, the day
  cycle in colors.h, race rules in race.h. Pins live in board.h.
  ═══════════════════════════════════════════════════════════════
*/

#ifndef CONFIG_H
#define CONFIG_H

// ═══════════════════════════════════════════════════════════════
//  SCREEN
// ═══════════════════════════════════════════════════════════════
#define SCR_W       320
#define SCR_H       240
#define SCR_CX      (SCR_W / 2)
#define SCR_CY      (SCR_H / 2)

// Show a small FPS / frame-time readout in the bottom-left corner
#define SHOW_FPS    1

// ═══════════════════════════════════════════════════════════════
//  WORLD
//  The track has segCount segments (track.h); MAX_SEGS (structs.h)
//  is only the capacity of the segment buffer.
// ═══════════════════════════════════════════════════════════════
#define SEG_LEN     200       // length of each segment (world units)
#define RUMBLE_LEN  3         // segments per rumble/stripe colour
#define DRAW_DIST   90        // draw distance in segments
#define ROAD_W      2000      // road half-width (world units)
#define LANES       3         // number of lanes
#define FOV_DEG     100       // field of view in degrees
#define CAM_HEIGHT  1000      // camera height
#define FOG_DENSITY 4.0f      // exponential fog density over DRAW_DIST
#define TUNNEL_H    2800      // tunnel ceiling height above the road

// ═══════════════════════════════════════════════════════════════
//  LATERAL LIMITS (road half-widths from the centre line).
//  Contract between the renderer and the physics: what is drawn
//  at these offsets is solid at these offsets.
// ═══════════════════════════════════════════════════════════════
#define ROAD_EDGE_X     1.00f  // tarmac ends here
#define RAIL_X          1.18f  // guard rails (SEGF_RAIL_L/R)
#define TUNNEL_WALL_X   1.20f  // tunnel walls
#define BUILDING_OFFSET 1.5f   // building / cliff / wall faces (Segment::build > 0)
#define OFFROAD_MAX_X   2.40f  // hard limit when nothing solid is closer

// Ground material layout beyond the road edge (render_road draws it this
// way, physics drags the car accordingly):
//   MAT_SIDEWALK: kerb up to SIDEWALK_KERB_X, sidewalk up to BUILDING_OFFSET,
//                 side-street asphalt beyond
//   MAT_SAND    : beach up to BEACH_WATER_X, the sea beyond
//   MAT_WATER   : sea-wall top up to SEAWALL_X, water beyond
//   others      : rumble strip, then the material
#define SIDEWALK_KERB_X 1.07f
#define BEACH_WATER_X   2.60f
#define SEAWALL_X       1.26f

// ═══════════════════════════════════════════════════════════════
//  BUILDINGS (geometry shared by the circuit generator, the
//  building renderer and the physics)
// ═══════════════════════════════════════════════════════════════
#define BUILDING_H_UNIT    2048    // Segment::build stores height / BUILDING_H_UNIT
#define BUILDING_FLOOR_H   11000   // height of one floor (same units)
#define BUILDING_W         400000  // depth of a building away from the road (px * scale)

// ═══════════════════════════════════════════════════════════════
//  PLAYER CAR LOOK
//  0 = original texture paint (navy). Otherwise an RGB565 colour that
//  replaces the paint, e.g. 0xD000 (red) or 0xFD20 (orange).
// ═══════════════════════════════════════════════════════════════
#define PLAYER_PAINT 0

#endif // CONFIG_H
