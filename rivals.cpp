/*
  ═══════════════════════════════════════════════════════════════
  RIVAL AI — implementation

  Per circuit (rivals_init):
    * racing line per segment: band-pass of the curvature (inside at the
      apex, outside before and after), clamped and smoothed, then made
      FEASIBLE by driving a reference car around it with the lateral
      speed/acceleration limits below (at full speed a car covers ~58
      segments per second, so an "ideal" line would need 3 half-widths/s
      of sideways speed in the city: it would look like snaking).
    * speed profile per segment: the speed at which the player's own car
      can still hold the corner (centrifugal drift = steering authority,
      the physics.cpp model) with a braking zone propagated backwards.
  Per rival: a personality (pace, cornering, aggression, consistency,
  preferred side, line amplitude) scaled by the difficulty.

  Per frame (think): target speed = min(top speed, corner profile, gap
  to the car ahead); lateral target = racing line + preference, or the
  passing line of a committed overtake; hard lateral bounds from every
  car alongside (never move into it) and the road limits. The lateral
  motion is a speed- and acceleration-limited controller with line
  feed-forward (no teleports, no jitter); the longitudinal motion is the
  player's own engine model with this rival's top speed.
  The player's autopilot runs the same think() (ai_drivePlayer).
  ═══════════════════════════════════════════════════════════════
*/

#include "rivals.h"
#include "race.h"
#include "physics.h"
#include "track.h"
#include "circuits.h"
#include "platform.h"
#include "utils.h"
#include "config.h"
#include <math.h>
#include <string.h>
#ifdef AI_DEBUG
#include <stdio.h>
#include <stdlib.h>
#endif

// ═══════════════════════════════════════════════════════════════
//  TUNING (wiki/11-rivals-ai.md explains every value)
// ═══════════════════════════════════════════════════════════════
// Racing line (segments, half-widths)
static const float LINE_SIGMA_NEAR = 12.0f;
static const float LINE_SIGMA_WIDE = 36.0f;
static const float LINE_WIDE_K     = 0.85f;
static const float LINE_GAIN       = 0.28f;   // half-widths per unit of curve
static const float LINE_MAX        = 0.70f;
static const float LINE_SIGMA_POST = 10.0f;
static const float LINE_REF_SPEED  = 0.90f;   // reference car: fraction of maxSpeed
static const float LINE_REF_VMAX   = 1.15f;   // its lateral limits (below the rivals')
static const float LINE_REF_AMAX   = 3.4f;
static const float LINE_REF_LEAD   = 0.15f;   // s
static const float LINE_REF_KP     = 4.0f;
static const float LINE_LEAD       = 0.06f;   // s: rivals sample the line slightly ahead
// Speed profile
static const float CORNER_MARGIN   = 0.92f;   // share of the steering authority a driver uses
static const float PROFILE_BRAKE   = 0.55f;   // share of BRAKE_DECEL planned for braking zones
// Lateral control (half-widths, seconds)
static const float LAT_VMAX        = 1.30f;   // normal driving
static const float LAT_VMAX_PASS   = 1.65f;   // overtaking / making room
static const float LAT_AMAX        = 4.0f;
static const float LAT_KP          = 3.0f;    // position feedback (1/s)
static const float X_LIMIT         = 0.80f;   // rivals drive within |x| <= X_LIMIT
// Traffic
static const float SEP_MIN         = 0.30f;   // centre distance kept from a car alongside (contact 0.24)
static const float PASS_SEP        = 0.38f;   // passing line: this far beside the passed car
static const float LANE_W          = 0.28f;   // |dx| below this: same lane (could touch)
static const float SEP_LOOKAHEAD   = 0.30f;   // s: bounds move with the other car's lateral speed
static const float GAP_MIN         = 180.0f;  // bumper gap always kept (world units)
static const float HEADWAY_CALM    = 0.16f;   // following gap in seconds, calm driver
static const float HEADWAY_AGGR    = 0.07f;   //                          aggressive driver
static const float GAP_K           = 2.2f;    // speed correction per unit of gap error (1/s)
// Rubber band (pace change, fraction): gap to the player in world units
static const float RB_DEAD         = 2500.0f;
static const float RB_RANGE        = 15000.0f;

// Difficulty tables: EASY, NORMAL, HARD
static const float BASE_PACE[3]  = { 0.955f, 1.000f, 1.030f };   // 1.0 = the player's car
static const float CORNER_ADJ[3] = { -0.03f, 0.00f, 0.01f };
static const float AGGR_SCALE[3] = { 0.60f, 0.85f, 1.00f };
static const float MISTAKE_P[3]  = { 0.55f, 0.15f, 0.03f };      // chance per check (6-14 s)
// Start reaction (s). The player's throttle is automatic (zero reaction), and
// at top speed one second is ~10 car lengths, so these small numbers already
// stagger the launch visibly.
static const float REACT_MIN[3]  = { 0.10f, 0.02f, 0.00f };
static const float REACT_MAX[3]  = { 0.35f, 0.16f, 0.10f };
static const float LAUNCH_P[3]   = { 0.10f, 0.30f, 0.55f };      // chance of a rocket start
static const float RB_AHEAD[3]   = { 0.035f, 0.020f, 0.010f };   // slower when far ahead of the player
static const float RB_BEHIND[3]  = { 0.005f, 0.012f, 0.020f };   // faster when far behind

// Personalities (index = racer; 0 is the player's autopilot)
struct Persona {
  const char* style;
  float pace;       // added to the difficulty's base pace
  float corner;     // corner speed factor
  float aggr;       // 0 calm .. 1 aggressive (gap, overtaking, turbo use)
  float consist;    // 0 erratic .. 1 metronome (form, wander, mistakes)
  float side;       // preferred lateral offset of its line
  float amp;        // share of the racing line it uses
};
static const Persona PERSONA[NUM_RACERS] = {
  { "BALANCED",    0.000f, 1.00f, 0.55f, 1.00f,  0.00f, 1.00f },   // autopilot
  { "AGGRESSIVE",  0.025f, 1.00f, 0.90f, 0.85f,  0.12f, 1.00f },   // VIPER
  { "SMOOTH",      0.015f, 1.03f, 0.35f, 0.95f, -0.22f, 0.90f },   // NOVA
  { "WILD",        0.005f, 0.98f, 1.00f, 0.55f,  0.28f, 1.10f },   // BLITZ
  { "DEFENSIVE",  -0.005f, 1.01f, 0.50f, 0.90f, -0.08f, 0.80f },   // RAVEN
  { "ERRATIC",    -0.015f, 0.97f, 0.75f, 0.65f, -0.30f, 1.00f },   // FURY
};

enum DriveMode : uint8_t { DM_LINE = 0, DM_FOLLOW, DM_PASS };

struct Driver {
  // personality (fixed for the race)
  float pace, corner, aggr, consist, side, amp;
  float reaction;          // start delay after GO (s)
  float mistakeP;          // chance of a mistake per check
  float wanderF, wanderPh, formPh;
  float gridX;             // grid column (held for the first seconds)
  bool  goodLaunch;
  // state
  bool  launched;
  uint8_t mode;
  int8_t  other;           // racer followed / being passed
  int8_t  passSide;        // -1 left, +1 right
  float modeT, passCool;
  float accel;             // throttle state (the player's `acceleration`)
  float xVel;              // lateral velocity (half-widths / s)
  float bias;              // current lateral preference
  float turbo, boostT, rocketT;
  float draft;             // smoothed slipstream 0..1
  float mistakeT, wideT, wideX, liftT;
  float brakeT;            // brake lights hold
  Rng   rng;
};
static Driver sDrv[NUM_RACERS];
static float  sPlayerXVel = 0;   // the player's measured lateral velocity (half-widths / s)
static float  sPlayerPrevX = 0;

static inline float perFrame(float f, float dt) { return powf(f, dt * 60.0f); }

// ═══════════════════════════════════════════════════════════════
//  PER-SEGMENT TABLES: racing line + speed profile
// ═══════════════════════════════════════════════════════════════
static float* sLine = nullptr;   // [MAX_SEGS] line x at each segment's near boundary
static float* sProf = nullptr;   // [MAX_SEGS] speed limit in each segment, fraction of maxSpeed
static int    sTabN = 0;         // segCount the tables describe (0 = none)

static bool allocTables() {
  if (sLine) return true;
  const size_t bytes = sizeof(float) * MAX_SEGS * 2;
  bool psram = true;
  float* p = (float*)plat_alloc_psram(bytes);     // read a few times per car per frame
  if (!p) { psram = false; p = (float*)plat_alloc_internal(bytes); }
  if (!p) { plat_log("ai: no memory for the racing line tables\n"); return false; }
  sLine = p;
  sProf = p + MAX_SEGS;
  plat_log("ai: racing line + speed profile (%u B) in %s\n", (unsigned)bytes,
           psram ? "PSRAM" : "internal SRAM");
  return true;
}

// Circular Gaussian smoothing. src has `stride` floats between entries.
static void gaussLoop(const float* src, int stride, float* dst, int n, float sigma) {
  static float w[121];
  int r = (int)ceilf(sigma * 3.0f);
  if (r > 120) r = 120;
  if (r > (n - 1) / 2) r = (n - 1) / 2;
  if (r < 0) r = 0;
  float sum = 0;
  for (int k = 0; k <= r; k++) {
    w[k] = expf(-0.5f * (float)(k * k) / (sigma * sigma));
    sum += (k ? 2.0f : 1.0f) * w[k];
  }
  if (sum <= 0) sum = 1;
  for (int k = 0; k <= r; k++) w[k] /= sum;
  for (int i = 0; i < n; i++) {
    float acc = w[0] * src[i * stride];
    for (int k = 1; k <= r; k++) {
      int a = i - k; if (a < 0) a += n;
      int b = i + k; if (b >= n) b -= n;
      acc += w[k] * (src[a * stride] + src[b * stride]);
    }
    dst[i] = acc;
  }
}

static inline float lerpLoop(const float* t, int n, float s) {   // s in segments
  float f = floorf(s);
  int i = (int)f % n;
  if (i < 0) i += n;
  int j = i + 1 >= n ? 0 : i + 1;
  float u = s - f;
  return t[i] + (t[j] - t[i]) * u;
}

// Speed fraction at which the player's car can still hold a corner of
// curvature c: the outward drift of updatePhysics (velocity term + direct
// term) equals CORNER_MARGIN of the steering authority.
static float cornerFraction(float c) {
  c = fabsf(c);
  if (c < 0.05f) return 1.0f;
  const float damp = -logf(LATERAL_FRICTION) * 60.0f;     // lateral damping rate (1/s)
  const float a = CENTRIFUGAL_DX * CENTRIFUGAL * c;         // s^2 term
  const float b = CENTRIFUGAL * CURVE_FORCE * c / damp;     // s term
  const float k = STEER_MANUAL * CORNER_MARGIN;
  float s = (-b + sqrtf(b * b + 4.0f * a * k)) / (2.0f * a);
  return clampF(s, 0.40f, 1.0f);
}

static void buildTables() {
  const int n = segCount;
  sTabN = 0;
  if (!sLine || n < 8) return;
  const int stride = (int)(sizeof(Segment) / sizeof(float));
  // 1. band-pass of the curvature: near-smoothed minus part of the wide-smoothed
  gaussLoop(&segments[0].curve, stride, sLine, n, LINE_SIGMA_NEAR);
  gaussLoop(&segments[0].curve, stride, sProf, n, LINE_SIGMA_WIDE);
  for (int i = 0; i < n; i++)
    sLine[i] = clampF(LINE_GAIN * (sLine[i] - LINE_WIDE_K * sProf[i]), -LINE_MAX, LINE_MAX);
  gaussLoop(sLine, 1, sProf, n, LINE_SIGMA_POST);              // smoothed ideal line -> sProf

  // 2. feasible line: a reference car tracks the ideal line at full speed with
  //    feed-forward + feedback under lateral limits; record its third lap.
  {
    const float segPerS = maxSpeed * LINE_REF_SPEED / SEG_LEN;
    const float step = 0.5f;                                     // segments per step
    const float dt = step / fmaxF(segPerS, 1.0f);
    float x = sProf[0], vx = 0.0f;
    for (float z = 0.0f; z < 3.0f * n; z += step) {
      float zl = z + LINE_REF_LEAD * segPerS;
      float tgt = lerpLoop(sProf, n, zl);
      float slope = (lerpLoop(sProf, n, zl + 0.5f) - lerpLoop(sProf, n, zl - 0.5f)) * segPerS;
      float want = clampF(slope + LINE_REF_KP * (tgt - x), -LINE_REF_VMAX, LINE_REF_VMAX);
      vx += clampF(want - vx, -LINE_REF_AMAX * dt, LINE_REF_AMAX * dt);
      x += vx * dt;
      if (z >= 2.0f * n) {
        int i = (int)z % n;
        sLine[i] = clampF(x, -LINE_MAX, LINE_MAX);
      }
    }
  }

  // 3. speed profile: corner limit under the car, then braking zones backwards
  for (int i = 0; i < n; i++) {
    float c = 0;
    for (int k = -1; k <= 2; k++) c = fmaxF(c, fabsf(segAt(i + k).curve));
    sProf[i] = cornerFraction(c);
  }
  const float kb = 2.0f * BRAKE_DECEL * PROFILE_BRAKE * SEG_LEN / (maxSpeed * maxSpeed);
  for (int pass = 0; pass < 2; pass++)
    for (int i = n - 1; i >= 0; i--) {
      float next = sProf[i + 1 >= n ? 0 : i + 1];
      float lim = sqrtf(next * next + kb);
      if (sProf[i] > lim) sProf[i] = lim;
    }
  sTabN = n;
}

static inline bool tablesOk() { return sTabN == segCount && sTabN > 0; }

static float lineAt(float z) {
  if (!tablesOk()) return 0.0f;
  return lerpLoop(sLine, sTabN, z * (1.0f / SEG_LEN));
}

// d(line)/dz in half-widths per world unit
static float lineSlope(float z) {
  if (!tablesOk()) return 0.0f;
  float s = z * (1.0f / SEG_LEN);
  return (lerpLoop(sLine, sTabN, s + 0.5f) - lerpLoop(sLine, sTabN, s - 0.5f)) * (1.0f / SEG_LEN);
}

static float profileAt(float z) {
  if (!tablesOk()) {   // fallback: the curvature just ahead
    float c = 0;
    int s = findSegIdx(z);
    for (int k = 0; k <= 3; k++) c = fmaxF(c, fabsf(segAt(s + k).curve));
    return cornerFraction(c);
  }
  return lerpLoop(sProf, sTabN, z * (1.0f / SEG_LEN));
}

// ═══════════════════════════════════════════════════════════════
//  SHARED HELPERS (interface)
// ═══════════════════════════════════════════════════════════════
float ai_lineTarget(float z, float bias) {
  return clampF(lineAt(z) + bias, -X_LIMIT, X_LIMIT);
}

float ai_cornerSpeed(float z) {
  // the profile just ahead of the car's front (it already holds the braking zones)
  return clampF(profileAt(z + CAR_LEN * 0.25f), 0.4f, 1.0f);
}

float ai_latVel(int i) {
  return (i >= 0 && i < NUM_RACERS) ? sDrv[i].xVel : 0.0f;
}

void ai_kickLatVel(int i, float dv) {
  if (i >= 0 && i < NUM_RACERS) sDrv[i].xVel = clampF(sDrv[i].xVel + dv, -2.5f, 2.5f);
}

float ai_turboMeter(int i) {
  if (i == PLAYER_ID) return turboMeter;
  return (i > 0 && i < NUM_RACERS) ? sDrv[i].turbo : 0.0f;
}

const char* ai_style(int i) {
  return (i >= 0 && i < NUM_RACERS) ? PERSONA[i].style : "";
}

// ═══════════════════════════════════════════════════════════════
//  INIT
// ═══════════════════════════════════════════════════════════════
void rivals_init() {
  if (allocTables()) buildTables();
  const int d = iclamp(difficulty, 0, 2);
  sPlayerXVel = 0.0f;
  sPlayerPrevX = racers[PLAYER_ID].x;
  for (int i = 0; i < NUM_RACERS; i++) {
    Driver& D = sDrv[i];
    D = Driver();                    // value-initialised: every field zero
    D.rng.setSeed(0x5EED0000u + (uint32_t)currentCircuit * 131u + (uint32_t)d * 7u + (uint32_t)i * 977u);
    const Persona& P = PERSONA[i];
    if (i == PLAYER_ID) {
      D.pace = 1.0f;
      D.corner = 1.0f;
      D.aggr = P.aggr;
      D.consist = 1.0f;
      D.side = 0.0f;
      D.amp = 1.0f;
      D.mistakeP = 0.0f;
    } else {
      D.pace = BASE_PACE[d] + P.pace + D.rng.frange(-0.003f, 0.003f);
      D.corner = P.corner + CORNER_ADJ[d];
      D.aggr = clampF(P.aggr * AGGR_SCALE[d], 0.0f, 1.0f);
      D.consist = P.consist;
      D.side = P.side;
      D.amp = P.amp;
      D.mistakeP = MISTAKE_P[d] * (1.3f - P.consist);
      // steadier drivers react more consistently
      float r = D.rng.uniform();
      r = lerpF(r, 0.5f, P.consist * 0.4f);
      D.reaction = lerpF(REACT_MIN[d], REACT_MAX[d], r);
      D.goodLaunch = D.rng.uniform() < LAUNCH_P[d] * (0.6f + 0.8f * P.consist * P.aggr);
    }
    D.wanderF = D.rng.frange(0.10f, 0.22f);
    D.wanderPh = D.rng.frange(0.0f, 6.2832f);
    D.formPh = D.rng.frange(0.0f, 6.2832f);
    D.gridX = racers[i].x;
    D.bias = racers[i].x;           // start from the grid column
    D.mode = DM_LINE;
    D.other = -1;
    D.passSide = 1;
    D.turbo = 1.0f;
    D.mistakeT = D.rng.frange(8.0f, 16.0f);
  }
}

// ═══════════════════════════════════════════════════════════════
//  DECISIONS
// ═══════════════════════════════════════════════════════════════
static float rubberBand(int i) {
  const Racer& me = racers[i];
  const Racer& P = racers[PLAYER_ID];
  if (i == PLAYER_ID || !raceRunning || P.finished || me.finished) return 0.0f;
  const int d = iclamp(difficulty, 0, 2);
  float gap = me.progress - P.progress;
  if (gap > RB_DEAD)  return -RB_AHEAD[d]  * clampF((gap - RB_DEAD) / RB_RANGE, 0.0f, 1.0f);
  if (gap < -RB_DEAD) return  RB_BEHIND[d] * clampF((-gap - RB_DEAD) / RB_RANGE, 0.0f, 1.0f);
  return 0.0f;
}

// Top speed of racer i before turbo (the player: maxSpeed, the physics caps it)
static float topSpeed(int i) {
  if (i == PLAYER_ID) return maxSpeed;
  const Driver& D = sDrv[i];
  float pace = D.pace * (1.0f + (1.0f - D.consist) * 0.012f * sinf(raceTime * 0.13f + D.formPh));
  pace *= 1.0f + rubberBand(i);
  pace *= 1.0f + DRAFT_BOOST * D.draft;           // slipstream, same as the player's
  if (racers[i].finished) pace *= 0.70f;          // cool-down lap
  return maxSpeed * pace;
}

// Signed curvature with the largest magnitude in the next `segs` segments
static float curveAhead(float z, int segs) {
  int s = findSegIdx(z);
  float best = 0;
  for (int k = 0; k < segs; k += 2) {
    float c = segAt(s + k).curve;
    if (fabsf(c) > fabsf(best)) best = c;
  }
  return best;
}

// Can racer i drive at lateral position x from its rear to dzEnd ahead?
static bool laneBlocked(int i, float x, int except, float dzEnd) {
  const Racer& me = racers[i];
  for (int k = 0; k < NUM_RACERS; k++) {
    if (k == i || k == except) continue;
    const Racer& o = racers[k];
    float dz = wrapDelta(me.z, o.z, trackLength);
    float rearReach = CAR_LEN * 1.1f + fmaxF(0.0f, o.speed - me.speed) * 0.9f;
    if (dz < -rearReach || dz > dzEnd + CAR_LEN * 1.6f) continue;
    if (fabsf(o.x - x) < SEP_MIN + 0.03f) return true;
  }
  return false;
}

// Bias wander, mistakes, turbo meter and burst timers (once per frame)
static void updateDriver(int i, float dt) {
  Driver& D = sDrv[i];
  const Racer& me = racers[i];
  float wander = (1.0f - D.consist) * 0.22f * sinf(raceTime * D.wanderF + D.wanderPh) +
                 0.05f * sinf(raceTime * 0.071f + (float)i * 1.9f);
  float target = me.finished ? D.side * 1.4f : D.side + wander;
  D.bias += (target - D.bias) * clampF(dt * 0.6f, 0.0f, 1.0f);
  D.draft += (physics_draftOf(i) - D.draft) * clampF(dt * 2.0f, 0.0f, 1.0f);

  if (raceRunning && !me.finished && me.progress > 0 && D.mistakeP > 0) {
    D.mistakeT -= dt;
    if (D.mistakeT <= 0) {
      D.mistakeT = D.rng.frange(6.0f, 14.0f);
      if (D.rng.uniform() < D.mistakeP) {
        float c = curveAhead(me.z, 26);
        if (fabsf(c) > 2.5f) {             // runs wide in the next corner
          D.wideT = D.rng.frange(1.0f, 1.8f);
          D.wideX = (c > 0 ? -1.0f : 1.0f) * D.rng.frange(0.18f, 0.32f);
        } else {                           // lifts for a moment
          D.liftT = D.rng.frange(0.6f, 1.2f);
        }
      }
    }
  }
  if (D.wideT > 0) D.wideT -= dt;
  if (D.liftT > 0) D.liftT -= dt;
  if (D.passCool > 0) D.passCool -= dt;
  if (D.boostT > 0) D.boostT -= dt;
  if (D.rocketT > 0) D.rocketT -= dt;
}

static void think(int i, float dt, AiCmd& out) {
  Racer& me = racers[i];
  Driver& D = sDrv[i];
  const bool isPlayer = (i == PLAYER_ID);
  const float v = me.speed;
  const float L = trackLength;

  // ---- 1. speed wanted: top speed, the corner profile, mistakes ----
  const float vTop = topSpeed(i);
  float vWant = fminF(vTop, maxSpeed * D.corner * profileAt(me.z + v * 0.10f));
  if (D.liftT > 0) vWant *= 0.93f;
  if (isPlayer && me.finished) vWant = fminF(vWant, maxSpeed * 0.62f);   // cool-down

  // ---- 2. preferred line: racing line + personal preference ----
  const float zl = me.z + v * LINE_LEAD;
  float tx = lineAt(zl) * D.amp + D.bias;
  float ff = lineSlope(zl) * v * D.amp;                 // feed-forward (half-widths / s)
  if (D.wideT > 0) tx += D.wideX;
  // right after the start: hold the grid column, then merge onto the line
  float merge = clampF((raceTime - D.reaction - 1.2f) / 2.5f, 0.0f, 1.0f);
  if (merge < 1.0f && me.progress < 6000.0f) {
    tx = lerpF(D.gridX, tx, merge);
    ff *= merge;
  }

  // ---- 3. look around ----
  float lo = -X_LIMIT, hi = X_LIMIT;          // hard bounds
  float slo = -1e9f, shi = 1e9f;              // soft bounds (cars just behind)
  lo = fmaxF(lo, 0.05f - physics_maxX(me.z, 0));
  hi = fminF(hi, physics_maxX(me.z, 1) - 0.05f);
  int follow = -1, block = -1;
  float followDz = 1e9f, blockDz = 1e9f;
  for (int j = 0; j < NUM_RACERS; j++) {
    if (j == i) continue;
    const Racer& o = racers[j];
    const float dz = wrapDelta(me.z, o.z, L);           // + : o is ahead
    const float dx = o.x - me.x;
    const float adx = fabsf(dx);
    // alongside, or diagonally just ahead: never move into it (hard bound)
    const float closing = dz > 0 ? v - o.speed : o.speed - v;
    const bool right = dx > 0.0f || (dx == 0.0f && j > i);
    if (fabsf(dz) < CAR_LEN ||
        (dz > 0 && dz < CAR_LEN + 120.0f + fmaxF(0.0f, closing) * 0.45f && adx >= LANE_W - 0.06f)) {
      // where it will be shortly if it keeps moving towards me
      const float ovx = (j == PLAYER_ID) ? sPlayerXVel : sDrv[j].xVel;
      if (right) hi = fminF(hi, o.x - SEP_MIN + fminF(0.0f, ovx) * SEP_LOOKAHEAD);
      else       lo = fmaxF(lo, o.x + SEP_MIN + fmaxF(0.0f, ovx) * SEP_LOOKAHEAD);
    } else if (dz < 0 && -dz < CAR_LEN + 380.0f + fmaxF(0.0f, closing) * 0.45f && adx >= LANE_W - 0.06f) {
      // diagonally just behind: do not cut in front of it, but it is its job
      // to avoid me, so it never pushes me aside nor slows me down (soft)
      if (right) shi = fminF(shi, fmaxF(o.x - SEP_MIN, me.x));
      else       slo = fmaxF(slo, fminF(o.x + SEP_MIN, me.x));
    }
    // ahead: in my lane (follow) or in my path (pass or follow)
    const float look = CAR_LEN + 700.0f + v * 0.30f + fmaxF(0.0f, v - o.speed) * 1.6f;
    if (dz > 0.0f && dz < look) {
      // follow only a car really in front: not one alongside (longitudinal
      // overlap) unless the two already touch laterally
      const bool inFront = dz > CAR_LEN - 40.0f || adx < 2.0f * CAR_HALF_W;
      if (adx < LANE_W && inFront && dz < followDz) { followDz = dz; follow = j; }
      if ((adx < PASS_SEP || fabsf(o.x - tx) < PASS_SEP) && dz < blockDz) { blockDz = dz; block = j; }
    }
  }

  // ---- 4. overtaking ----
  D.modeT += dt;
  if (D.mode == DM_PASS && D.other >= 0) {
    const Racer& o = racers[D.other];
    const float dz = wrapDelta(me.z, o.z, L);
    const float px = o.x + D.passSide * PASS_SEP;
    const bool done = dz < -CAR_LEN * 0.8f || D.modeT > 7.0f;
    // give up when the lane closes, when it pulls away, or when side by side
    // nothing is gained (a stalemate only ends in contact)
    const bool abortPass = fabsf(px) > X_LIMIT + 0.03f || laneBlocked(i, px, D.other, dz) ||
                           (dz > CAR_LEN && o.speed > v + 150.0f && D.modeT > 1.2f) ||
                           (D.modeT > 3.5f && v < o.speed + 40.0f);
    if (done) {
      D.mode = DM_LINE;
    } else if (abortPass) {
      D.mode = DM_FOLLOW;
      D.passCool = lerpF(1.6f, 0.5f, D.aggr);
    } else {
      tx = px;
      ff = 0.0f;
    }
  }
  if (D.mode != DM_PASS) {
    D.mode = DM_LINE;
    D.other = -1;
    if (block >= 0) {
      const Racer& o = racers[block];
      // worth passing only if this car really is quicker: closing now, or
      // its own cruise speed (pace + slipstream) beats the other's speed
      const float myCruise = isPlayer ? maxSpeed * ACCEL_NEAR_MAX * (1.0f + DRAFT_BOOST * draft)
                                      : vTop * ACCEL_NEAR_MAX;
      const bool faster = (v > o.speed + 150.0f || fminF(myCruise, vWant) > o.speed + 100.0f) &&
                          vWant > o.speed + 120.0f;
      if (faster && D.passCool <= 0.0f && raceRunning) {
        const float lx = o.x - PASS_SEP, rx = o.x + PASS_SEP;
        const bool okL = lx >= -X_LIMIT && lx >= lo - 0.02f && !laneBlocked(i, lx, block, blockDz);
        const bool okR = rx <=  X_LIMIT && rx <= hi + 0.02f && !laneBlocked(i, rx, block, blockDz);
        if (okL || okR) {
          int s;
          if (okL && okR) {
            // the side of my racing line (the inside of what comes), then the nearer one
            float costL = fabsf(lx - tx) + 0.6f * fabsf(lx - me.x) - (D.side < 0 ? 0.04f : 0.0f);
            float costR = fabsf(rx - tx) + 0.6f * fabsf(rx - me.x) - (D.side > 0 ? 0.04f : 0.0f);
            s = costL <= costR ? -1 : 1;
          } else {
            s = okL ? -1 : 1;
          }
          D.mode = DM_PASS;
          D.other = (int8_t)block;
          D.passSide = (int8_t)s;
          D.modeT = 0.0f;
          tx = o.x + s * PASS_SEP;
          ff = 0.0f;
        } else {
          D.mode = DM_FOLLOW;
          D.other = (int8_t)block;
        }
      } else if (follow == block) {
        D.mode = DM_FOLLOW;
        D.other = (int8_t)block;
      }
    }
  }

  // ---- 5. keep a gap to the car ahead in my lane ----
  float vTraffic = 1e9f;
  if (follow >= 0) {
    const Racer& o = racers[follow];
    float gap = followDz - CAR_LEN;                       // bumper to bumper
    float want = GAP_MIN + v * lerpF(HEADWAY_CALM, HEADWAY_AGGR, D.aggr);
    if (D.mode == DM_PASS) want *= 0.6f;                  // tuck in while pulling out
    // open a short gap gently (lift); only an unsafe gap asks for real braking
    float corr = (gap - want) * GAP_K;
    corr = fmaxF(corr, gap < GAP_MIN * 1.5f ? -2500.0f : -700.0f);
    vTraffic = fmaxF(0.0f, o.speed + corr);
  }

  // ---- 6. lateral target within the bounds ----
  bool squeezed = lo > hi;
  if (squeezed) {
    tx = 0.5f * (lo + hi);
    vTraffic = fminF(vTraffic, v * 0.985f);      // cars alongside on both sides: back off
  } else {
    tx = clampF(tx, lo, hi);
    const float l2 = fmaxF(lo, slo), h2 = fminF(hi, shi);
    if (l2 <= h2) { tx = clampF(tx, l2, h2); lo = l2; hi = h2; }
  }
  bool evading = D.mode == DM_PASS || squeezed || me.x < lo || me.x > hi;
  float vmax = evading ? LAT_VMAX_PASS : LAT_VMAX;
  vmax *= clampF(v / (0.30f * maxSpeed), 0.15f, 1.0f);   // no sideways slide when slow
  float wantV = clampF(ff + LAT_KP * (tx - me.x), -vmax, vmax);
  // never faster towards a bound than what can still stop before it (the
  // acceleration limit would otherwise overshoot into the car alongside)
  if (!squeezed) {
    wantV = fminF(wantV, sqrtf(2.0f * LAT_AMAX * fmaxF(0.0f, hi - me.x)));
    wantV = fmaxF(wantV, -sqrtf(2.0f * LAT_AMAX * fmaxF(0.0f, me.x - lo)));
  }
  D.xVel += clampF(wantV - D.xVel, -LAT_AMAX * dt, LAT_AMAX * dt);

  // ---- 7. turbo: bursts on straights, more willingly when passing ----
  bool turbo = false;
  if (raceRunning && !me.finished) {
    if (D.boostT > 0.0f) {
      turbo = true;
    } else if (D.turbo > 0.55f && v > vTop * 0.80f && follow < 0 &&
               ai_cornerSpeed(me.z) > 0.99f && profileAt(me.z + 5000.0f) > 0.99f) {
      float p = (0.08f + 0.45f * D.aggr) * dt * (D.mode == DM_PASS ? 3.0f : 1.0f);
      if (D.rng.uniform() < p) D.boostT = D.turbo * (1.0f / TURBO_DRAIN) * D.rng.frange(0.5f, 1.0f);
    }
  }

#ifdef AI_DEBUG
  // host-only decision trace: AI_DEBUG=<racer> in the environment
  static int dbg = -2;
  if (dbg == -2) { const char* e = getenv("AI_DEBUG"); dbg = e ? atoi(e) : -1; }
  if (dbg == i)
    printf("AI %d t=%.2f z=%.0f x=%.3f v=%.0f vTop=%.0f vWant=%.0f vTraf=%.0f mode=%d oth=%d fol=%d blk=%d "
           "tx=%.3f lo=%.3f hi=%.3f acc=%.0f xv=%.3f turbo=%d draft=%.2f wide=%.1f lift=%.1f\n",
           i, raceTime, me.z, me.x, v, vTop, vWant, vTraffic, D.mode, D.other, follow, block, tx, lo, hi,
           D.accel, D.xVel, (int)turbo, D.draft, D.wideT, D.liftT);
#endif
  // Corners work like for a human (the throttle is automatic): brake only
  // when clearly over the corner speed. Traffic: ease off, brake if needed.
  out.xTarget = tx;
  out.xVel = D.xVel;
  out.vTarget = vWant;
  out.vTraffic = vTraffic;
  out.brake = v > vWant + AI_CORNER_TOL || v > vTraffic + 220.0f;
  if (out.brake) { turbo = false; D.boostT = 0.0f; }      // never boost into a braking zone
  out.turbo = turbo;
}

// ═══════════════════════════════════════════════════════════════
//  MOTION (rivals): the player's engine model with this car's top speed
// ═══════════════════════════════════════════════════════════════
static void moveRival(int i, const AiCmd& c, float dt) {
  Racer& me = racers[i];
  Driver& D = sDrv[i];

  // turbo meter (same rules as the player's), rocket-start boost is free
  bool boost = (c.turbo && D.turbo > 0.01f && !c.brake) || D.rocketT > 0.0f;
  if (boost && D.rocketT <= 0.0f) D.turbo = fmaxF(0.0f, D.turbo - TURBO_DRAIN * dt);
  else if (!boost) D.turbo = fminF(1.0f, D.turbo + TURBO_REFILL * dt);
  if (!boost) D.boostT = 0.0f;
  me.boosting = boost;

  // lateral
  me.x += D.xVel * dt;
  float yawT = clampF(atan2f(D.xVel * ROAD_W, fmaxF(me.speed, 1500.0f)), -0.45f, 0.45f);
  me.yaw += (yawT - me.yaw) * clampF(dt * 10.0f, 0.0f, 1.0f);

  // longitudinal
  const float tmul = boost ? TURBO_ACCEL : 1.0f;
  const float top = topSpeed(i) * (boost ? 1.0f + TURBO_BOOST : 1.0f);
  const float v0 = me.speed;
  // Lifting to keep a gap is the driver's foot, not the engine: the engine
  // state is held (the player's ramp is slow, so letting it decay made every
  // lift cost seconds), it just does not push while the foot is up.
  const bool lifting = !c.brake && me.speed >= c.vTraffic;
  if (c.brake) {
    D.accel *= perFrame(BRAKE_ACCEL_KEEP, dt);
    float over = me.speed - fminF(c.vTarget, c.vTraffic);
    float decel = clampF(over * 3.0f, 2500.0f, BRAKE_DECEL);
    me.speed -= fminF(decel * dt, fmaxF(over, 0.0f));
  } else if (!lifting) {
    if (me.speed < top * ACCEL_NEAR_MAX) {
      D.accel = fminF(D.accel + ACCEL_RAMP * tmul * dt, maxSpeed * ACCEL_TARGET * tmul);
    } else {
      // at the top: exactly the player's rule (ease down to the throttle
      // that holds this car's cruise speed)
      D.accel = fmaxF(D.accel * perFrame(ACCEL_DAMPING, dt),
                      fminF(D.accel, physics_cruiseAccel(topSpeed(i))));
    }
  }
  const int seg = findSegIdx(me.z);
  const float slope = (segAt(seg).y - segAt(seg - 1).y) / SEG_LEN;
  me.speed *= perFrame(FRICTION, dt);
  if (!lifting) me.speed += D.accel * dt;
  me.speed -= slope * GRAVITY_FACTOR * dt;
  if (me.speed > top) me.speed -= (me.speed - top) * clampF(dt * 2.0f, 0.0f, 1.0f);
  if (me.speed < 0.0f) me.speed = 0.0f;

  // brake lights: braking, or a notable deceleration (held a moment)
  float decelNow = dt > 0.0f ? (v0 - me.speed) / dt : 0.0f;
  if (c.brake || decelNow > 2600.0f) D.brakeT = 0.25f;
  else if (D.brakeT > 0.0f) D.brakeT -= dt;
  me.braking = D.brakeT > 0.0f;

  me.z = loopIncrease(me.z, me.speed * dt, trackLength);
}

// Waiting on the grid: engine off, foot on the brake
static void hold(int i) {
  Racer& me = racers[i];
  Driver& D = sDrv[i];
  me.speed = 0;
  me.yaw = 0;
  me.boosting = false;
  me.braking = true;
  D.accel = 0;
  D.xVel = 0;
}

// ═══════════════════════════════════════════════════════════════
//  UPDATE
// ═══════════════════════════════════════════════════════════════
void rivals_update(float dt) {
  if (segCount <= 0) return;
  race_syncPlayer();
  if (dt > 0.0f) {
    float vx = (racers[PLAYER_ID].x - sPlayerPrevX) / dt;
    sPlayerXVel += (clampF(vx, -3.0f, 3.0f) - sPlayerXVel) * clampF(dt * 12.0f, 0.0f, 1.0f);
  }
  sPlayerPrevX = racers[PLAYER_ID].x;

  static AiCmd cmd[NUM_RACERS];
  static bool  act[NUM_RACERS];
  for (int i = 1; i < NUM_RACERS; i++) {
    Driver& D = sDrv[i];
    act[i] = false;
    if (!raceRunning) { hold(i); continue; }
    if (!D.launched) {
      if (raceTime < D.reaction) { hold(i); continue; }
      D.launched = true;
      racers[i].braking = false;
      if (D.goodLaunch) { D.accel = ROCKET_ACCEL; D.rocketT = ROCKET_TIME; }
    }
    updateDriver(i, dt);
    think(i, dt, cmd[i]);       // everybody decides on the same snapshot...
    act[i] = true;
  }
  for (int i = 1; i < NUM_RACERS; i++)
    if (act[i]) moveRival(i, cmd[i], dt);   // ...then everybody moves

  physics_resolveContacts(dt);
}

void ai_drivePlayer(float dt, AiCmd& out) {
  race_syncPlayer();
  Driver& D = sDrv[PLAYER_ID];
  D.turbo = turboMeter;
  updateDriver(PLAYER_ID, dt);
  think(PLAYER_ID, dt, out);
}
