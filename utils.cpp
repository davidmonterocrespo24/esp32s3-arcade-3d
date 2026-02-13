/*
  ═══════════════════════════════════════════════════════════════
  UTILITY FUNCTIONS IMPLEMENTATION
  ═══════════════════════════════════════════════════════════════
*/

#include "utils.h"
#include "config.h"
#include <math.h>

// ═══════════════════════════════════════════════════════════════
//  FUNCTION IMPLEMENTATION
// ═══════════════════════════════════════════════════════════════

float clampF(float v, float lo, float hi) {
  return (v < lo) ? lo : (v > hi) ? hi : v;
}

float lerpF(float a, float b, float t) {
  return a + (b - a) * t;
}

float easeIn(float a, float b, float t) {
  return a + (b - a) * t * t;
}

float easeInOut(float a, float b, float t) {
  return a + (b - a) * (-cosf(t * PI) * 0.5f + 0.5f);
}

float loopIncrease(float v, float inc, float mx) {
  float r = v + inc;
  while (r >= mx) r -= mx;
  while (r < 0)   r += mx;
  return r;
}

float percentRemaining(float v, float total) {
  float r = fmodf(v, total);
  if (r < 0) r += total;
  return r / total;
}

int findSegIdx(float z) {
  int i = (int)floorf(z / SEG_LEN) % TOTAL_SEGS;
  if (i < 0) i += TOTAL_SEGS;
  return i;
}

float expFog(float d, float density) {
  return 1.0f - clampF(1.0f / expf(d * d * density), 0, 1);
}

bool overlapChk(float x1, float w1, float x2, float w2) {
  float h1 = w1 * 0.5f, h2 = w2 * 0.5f;
  return !((x1 + h1) < (x2 - h2) || (x1 - h1) > (x2 + h2));
}
