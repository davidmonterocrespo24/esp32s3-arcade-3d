/*
  ═══════════════════════════════════════════════════════════════
  GAME CONFIGURATION AND CONSTANTS
  Pins live in board.h.
  ═══════════════════════════════════════════════════════════════
*/

#ifndef CONFIG_H
#define CONFIG_H

// ═══════════════════════════════════════════════════════════════
//  SCREEN CONSTANTS
// ═══════════════════════════════════════════════════════════════
#define SCR_W       320
#define SCR_H       240
#define SCR_CX      (SCR_W / 2)
#define SCR_CY      (SCR_H / 2)

// Show a small FPS / frame-time readout in the bottom-left corner
#define SHOW_FPS    1

// ═══════════════════════════════════════════════════════════════
//  GAME CONSTANTS
// ═══════════════════════════════════════════════════════════════
#define SEG_LEN     200       // Length of each segment
#define RUMBLE_LEN  3         // Segments per rumble/stripe colour
#define DRAW_DIST   90        // Draw distance in segments (was 40)
#define TOTAL_SEGS  1000      // Track length: 16 B/segment -> 16 KB (was 200 = 3 s laps)
#define ROAD_W      2000      // Road half-width
#define LANES       3         // Number of lanes
#define FOV_DEG     100       // Field of view in degrees
#define CAM_HEIGHT  1000      // Camera height
#define FOG_DENSITY 4.0f      // Exponential fog density over DRAW_DIST
#define TUNNEL_H    2800      // Tunnel ceiling height above the road (was 4500 = 23 m)
#define TOTAL_LAPS  3

// 1 = random track on startup, 0 = fixed track
#define RANDOM_TRACK 1

// ═══════════════════════════════════════════════════════════════
//  DAY / NIGHT CYCLE
//  The cycle is day -> sunset -> night -> dawn -> day, driven by
//  distance travelled. Each phase holds, then blends into the next.
// ═══════════════════════════════════════════════════════════════
#define CYCLE_PHASE_DIST   (TOTAL_SEGS * SEG_LEN * 1.25f) // distance per phase
#define CYCLE_BLEND        0.30f   // last 30% of each phase blends to the next
#define CYCLE_START        0.0f    // 0=day 1=sunset 2=night 3=dawn

// ═══════════════════════════════════════════════════════════════
//  CAR PHYSICS (per-frame factors are defined at 60 fps and are
//  rescaled by dt, so tuning no longer depends on frame rate)
// ═══════════════════════════════════════════════════════════════
#define SPEED_MULTIPLIER   65.0f   // maxSpeed = SEG_LEN * SPEED_MULTIPLIER (~246 km/h)
#define ACCEL_TARGET       0.9f    // targetAccel = maxSpeed * ACCEL_TARGET
#define ACCEL_RAMP         420.0f  // How fast acceleration ramps up (u/s³)
#define ACCEL_NEAR_MAX     0.90f   // Speed threshold to reduce acceleration
#define ACCEL_DAMPING      0.97f   // Multiplier when approaching maxSpeed (per 60 Hz frame)
#define FRICTION           0.996f  // Friction per 60 Hz frame (~3.3 s coast-down)
#define BRAKE_DECEL        9000.0f // Brake deceleration (u/s²)
#define GRAVITY_FACTOR     1600.0f // Effect of slopes on acceleration
#define CENTRIFUGAL        0.18f   // Centrifugal force in curves
#define CURVE_FORCE        3.0f    // Lateral force multiplier in curves
#define LATERAL_FRICTION   0.90f   // Lateral velocity damping per 60 Hz frame
#define STEER_AUTO         1.8f    // Autopilot response to curves
#define STEER_MANUAL       2.2f    // Manual steering speed (road half-widths / s)
#define CENTRIFUGAL_DX     1.5f    // Lateral centrifugal push factor
#define AUTOPILOT_IDLE_MS  8000    // No input for this long -> autopilot (demo)

// ═══════════════════════════════════════════════════════════════
//  BUILDINGS
// ═══════════════════════════════════════════════════════════════
#define BUILDING_H_UNIT    2048    // Segment stores height / BUILDING_H_UNIT in a byte
#define BUILDING_H_MIN     120000  // Minimum building height (~10 floors)
#define BUILDING_H_MAX     350000  // Maximum building height (~30 floors)
#define BUILDING_FLOOR_H   11000   // Height of one floor (same units)
#define BUILDING_W         400000  // Base building width
#define BUILDING_OFFSET    1.5f    // Distance from road edge
#define BUILDING_SEG_MIN   6       // Minimum segments per block
#define BUILDING_SEG_MAX   16      // Maximum segments per block
#define BUILDING_GAP_MIN   10      // Minimum gap segments between blocks
#define BUILDING_GAP_MAX   20      // Maximum gap segments between blocks

// ═══════════════════════════════════════════════════════════════
//  TRAFFIC
// ═══════════════════════════════════════════════════════════════
#define MAX_CARS 10

// ═══════════════════════════════════════════════════════════════
//  PLAYER CAR LOOK
//  0 = original texture paint (navy). Otherwise an RGB565 colour that
//  replaces the paint, e.g. 0xD000 (red) or 0xFD20 (orange).
// ═══════════════════════════════════════════════════════════════
#define PLAYER_PAINT 0

#endif // CONFIG_H
