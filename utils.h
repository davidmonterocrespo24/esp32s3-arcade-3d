/*
  ═══════════════════════════════════════════════════════════════
  UTILITY FUNCTIONS
  ═══════════════════════════════════════════════════════════════
*/

#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>

#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif

// Portable min/max (Arduino-ESP32 3.x maps min/max to std:: templates that
// refuse mixed int/float arguments; the emulator does the same).
static inline int   imin(int a, int b)   { return a < b ? a : b; }
static inline int   imax(int a, int b)   { return a > b ? a : b; }
static inline int   iclamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float fminF(float a, float b) { return a < b ? a : b; }
static inline float fmaxF(float a, float b) { return a > b ? a : b; }

// Clamp a value between a minimum and maximum
float clampF(float v, float lo, float hi);

// Linear interpolation
float lerpF(float a, float b, float t);

// Ease-in interpolation
float easeIn(float a, float b, float t);

// Ease-in-out interpolation
float easeInOut(float a, float b, float t);

// Increment a value with wrap-around into [0, mx)
float loopIncrease(float v, float inc, float mx);

// Fraction of the way through the current cycle (0..1)
float percentRemaining(float v, float total);

// Segment index of track position z (wraps; 0 when no track is loaded)
int findSegIdx(float z);

// Calculate exponential fog
float expFog(float d, float density);

// Check overlap between two objects (centres and full widths)
bool overlapChk(float x1, float w1, float x2, float w2);

// Signed shortest distance a -> b on a loop of length len, in [-len/2, len/2)
float wrapDelta(float a, float b, float len);

// Deterministic integer hash (window patterns, star twinkle, ...)
static inline uint32_t hash32(uint32_t x) {
  x ^= x >> 16; x *= 0x7FEB352Du;
  x ^= x >> 15; x *= 0x846CA68Bu;
  x ^= x >> 16;
  return x;
}

// ═══════════════════════════════════════════════════════════════
//  DETERMINISTIC RANDOM NUMBERS
//  Circuits, scenery and backgrounds are generated from a seeded Rng,
//  never from Arduino's random(), so a circuit is identical every time
//  and headless runs are reproducible. (xorshift32; never yields 0.)
// ═══════════════════════════════════════════════════════════════
struct Rng {
  uint32_t s;
  explicit Rng(uint32_t seed = 1u) { setSeed(seed); }
  void setSeed(uint32_t seed) { s = hash32(seed ^ 0xA5A5F00Du); if (!s) s = 0x9E3779B9u; }
  uint32_t next() {
    uint32_t x = s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return s = x;
  }
  // integer in [lo, hi) — returns lo when the range is empty
  int range(int lo, int hi) {
    if (hi <= lo) return lo;
    return lo + (int)(next() % (uint32_t)(hi - lo));
  }
  // float in [0, 1)
  float uniform() { return (float)(next() >> 8) * (1.0f / 16777216.0f); }
  // float in [lo, hi)
  float frange(float lo, float hi) { return lo + (hi - lo) * uniform(); }
  bool  chance(float p) { return uniform() < p; }
  // -1 or +1
  int   sign() { return (next() & 0x100) ? 1 : -1; }
};

#endif // UTILS_H
