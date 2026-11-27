/*
  ═══════════════════════════════════════════════════════════════
  RACE STATE: racers, grid, progress, laps, finish, places, results
  The player's car physics live in physics.cpp (position is the
  camera; the car is at position + playerZdist); race_update mirrors
  the player into racers[PLAYER_ID] every frame.
  ═══════════════════════════════════════════════════════════════
*/

#ifndef RACE_H
#define RACE_H

#include <stdint.h>

#define NUM_RIVALS 5
#define NUM_RACERS (NUM_RIVALS + 1)
#define PLAYER_ID  0

// Car footprint in world terms, derived from the renderer (not guessed):
// a car is drawn with pxPerUnit = scale * 27000 and the road half-width is
// scale * ROAD_W * SCR_CX px, so the 2.9 x 6.5-unit model is 0.245 road
// half-widths wide and ~1100 world units (~5.5 segments) long.
#define CAR_HALF_W   0.12f              // road half-widths
#define CAR_LEN      1100.0f            // world units
#define GRID_X       0.45f              // grid columns at x = -0.45 / +0.45
#define GRID_ROW     (CAR_LEN * 1.5f)   // distance between cars of the same column
#define GRID_FRONT   (CAR_LEN * 0.5f + 150.0f)   // pole car centre behind the line

struct Racer {
  char     name[8];
  uint16_t paint;              // RGB565 (player: PLAYER_PAINT, 0 = texture paint)
  float    z;                  // track position of the car centre, [0, trackLength)
  float    x;                  // lateral offset, road half-widths
  float    speed;
  float    yaw;                // visual heading relative to the road (radians, + = nose right)
  float    progress;           // race distance from the start line (negative on the grid)
  int      lap;                // completed laps
  float    lapStart, lastLap, bestLap;
  bool     finished;
  float    finishTime;
  int      place;              // 1-based, updated every frame
  bool     braking, boosting;
};

extern Racer racers[NUM_RACERS];
extern float raceTime;         // seconds since GO (0 during the countdown)
extern bool  raceRunning;      // false on the grid / countdown
extern int   difficulty;       // 0 easy, 1 normal, 2 hard

void  race_init();             // after buildCircuit(): grid positions, names, paints, reset times/laps
void  race_go();               // the countdown says GO
void  race_update(float dt);   // after physics + rivals: progress, laps, finish, places
float race_distance();         // laps * trackLength
int   race_order(int place);   // racer index at 1-based place

// Final classification, in place order: finished racers by finish time,
// the others with an estimated time (remaining distance / average pace).
struct ResultRow {
  uint8_t racer;
  bool    finished;            // false: time is an estimate
  float   time;
  float   bestLap;
};
void  race_results(ResultRow out[NUM_RACERS]);
// Start progress of a grid slot (0 = pole ... NUM_RACERS-1 = last) and its column
float race_gridProgress(int slot);
float race_gridX(int slot);

// ═══════════════════════════════════════════════════════════════
//  ADDITIONS (feature phase)
// ═══════════════════════════════════════════════════════════════
// Copy the player's physics state (position + playerZdist, playerX, speed,
// yaw, brake, turbo) into racers[PLAYER_ID]. race_update() does it; the
// rival AI calls it first so it sees the player of THIS frame.
void  race_syncPlayer();
// Seconds since racer i started its current lap (0 before the start)
float race_lapTime(int i);

#endif // RACE_H
