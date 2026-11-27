/*
  ═══════════════════════════════════════════════════════════════
  UTILITY FUNCTIONS IMPLEMENTATION
  ═══════════════════════════════════════════════════════════════
*/

#include "utils.h"
#include "config.h"
#include "track.h"
#include <math.h>

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
  return a + (b - a) * (-cosf(t * (float)PI) * 0.5f + 0.5f);
}

float loopIncrease(float v, float inc, float mx) {
  if (!(mx > 0)) return v + inc;
  float r = v + inc;
  if (r >= mx || r < 0) {
    r = fmodf(r, mx);
    if (r < 0) r += mx;
    if (r >= mx) r = 0;   // fmodf rounding at the top edge
  }
  return r;
}

float percentRemaining(float v, float total) {
  if (!(total > 0)) return 0;
  float r = fmodf(v, total);
  if (r < 0) r += total;
  return r / total;
}

int findSegIdx(float z) {
  if (segCount <= 0) return 0;            // integer % 0 reboots the ESP32
  if (!(z == z)) return 0;                // NaN guard
  int i = (int)floorf(z / SEG_LEN) % segCount;
  if (i < 0) i += segCount;
  return i;
}

float expFog(float d, float density) {
  return 1.0f - clampF(1.0f / expf(d * d * density), 0, 1);
}

bool overlapChk(float x1, float w1, float x2, float w2) {
  float h1 = w1 * 0.5f, h2 = w2 * 0.5f;
  return !((x1 + h1) < (x2 - h2) || (x1 - h1) > (x2 + h2));
}

float wrapDelta(float a, float b, float len) {
  float d = b - a;
  if (!(len > 0)) return d;
  float half = len * 0.5f;
  if (d >= half || d < -half) {
    d = fmodf(d + half, len);
    if (d < 0) d += len;
    d -= half;
  }
  return d;
}
