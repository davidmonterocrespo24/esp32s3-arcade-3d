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
// refuse mixed int/float arguments; the emulator maps them to macros).
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

// Increment a value with wrap-around
float loopIncrease(float v, float inc, float mx);

// Fraction of the way through the current cycle (0..1)
float percentRemaining(float v, float total);

// Find segment index by Z position
int findSegIdx(float z);

// Calculate exponential fog
float expFog(float d, float density);

// Check overlap between two objects
bool overlapChk(float x1, float w1, float x2, float w2);

// Deterministic integer hash (window patterns, star twinkle, ...)
static inline uint32_t hash32(uint32_t x) {
  x ^= x >> 16; x *= 0x7FEB352Du;
  x ^= x >> 15; x *= 0x846CA68Bu;
  x ^= x >> 16;
  return x;
}

#endif // UTILS_H
