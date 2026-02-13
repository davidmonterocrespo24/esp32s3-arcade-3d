/*
  ═══════════════════════════════════════════════════════════════
  GAME CONFIGURATION AND CONSTANTS
  ═══════════════════════════════════════════════════════════════
*/

#ifndef CONFIG_H
#define CONFIG_H

// ═══════════════════════════════════════════════════════════════
//  HARDWARE PINS (Screen pins go in User_Setup.h)
// ═══════════════════════════════════════════════════════════════
#define TFT_BL    39

// Buttons
#define BTN_LEFT  17
#define BTN_RIGHT 16

// ═══════════════════════════════════════════════════════════════
//  SCREEN CONSTANTS
// ═══════════════════════════════════════════════════════════════
#define SCR_W       320
#define SCR_H       240
#define SCR_CX      (SCR_W / 2)
#define SCR_CY      (SCR_H / 2)

// ═══════════════════════════════════════════════════════════════
//  GAME CONSTANTS
// ═══════════════════════════════════════════════════════════════
#define SEG_LEN     200       // Length of each segment
#define RUMBLE_LEN  3         // Length of rumble strips
#define DRAW_DIST   40        // Draw distance
#define TOTAL_SEGS  200       // Total segments on the track (RAM optimized)
#define ROAD_W      2000      // Road width
#define LANES       3         // Number of lanes
#define FOV_DEG     100       // Field of view in degrees
#define CAM_HEIGHT  1000      // Camera height
#define FOG_DENSITY 5         // Fog density

// 1 = random track on startup, 0 = fixed track
#define RANDOM_TRACK 1

// ═══════════════════════════════════════════════════════════════
//  CAR PHYSICS
// ═══════════════════════════════════════════════════════════════
#define SPEED_MULTIPLIER   65.0f   // maxSpeed = SEG_LEN * SPEED_MULTIPLIER (~246 km/h)
#define ACCEL_TARGET       0.9f    // targetAccel = maxSpeed * ACCEL_TARGET
#define ACCEL_RAMP         180.0f  // How fast acceleration ramps up (u/s²)
#define ACCEL_NEAR_MAX     0.90f   // Speed threshold to reduce acceleration
#define ACCEL_DAMPING      0.97f   // Multiplier when approaching maxSpeed
#define FRICTION           0.996f  // Friction per frame (braking ~3.3s from max)
#define GRAVITY_FACTOR     1600.0f // Effect of slopes on acceleration
#define CENTRIFUGAL        0.18f   // Centrifugal force in curves
#define CURVE_FORCE        3.0f    // Lateral force multiplier in curves
#define LATERAL_FRICTION   0.90f   // Lateral velocity damping per frame
#define STEER_AUTO         1.8f    // Autopilot response to curves
#define CENTRIFUGAL_DX     1.5f    // Lateral centrifugal push factor

// ═══════════════════════════════════════════════════════════════
//  BUILDINGS
// ═══════════════════════════════════════════════════════════════
#define BUILDING_H_MIN     120000  // Minimum building height (~10 floors)
#define BUILDING_H_MAX     350000  // Maximum building height (~30 floors)
#define BUILDING_W         400000  // Base building width
#define BUILDING_OFFSET    1.5f    // Distance from road edge
#define BUILDING_SEG_MIN   6       // Minimum segments per block
#define BUILDING_SEG_MAX   16      // Maximum segments per block
#define BUILDING_GAP_MIN   10      // Minimum gap segments between blocks
#define BUILDING_GAP_MAX   20      // Maximum gap segments between blocks

// ═══════════════════════════════════════════════════════════════
//  RACE / COMPETITORS
// ═══════════════════════════════════════════════════════════════
#define MAX_CARS        0   // No random traffic cars in race mode
#define NUM_COMPETITORS 3   // Number of AI race competitors

#endif // CONFIG_H
