/*
  ═══════════════════════════════════════════════════════════════
  TRACK STORAGE AND LOW-LEVEL BUILDERS — implementation
  ═══════════════════════════════════════════════════════════════
*/

#include "track.h"
#include "utils.h"
#include "platform.h"
#include <math.h>
#include <string.h>

Segment* segments    = nullptr;
int      segCount    = 0;
float    trackLength = SEG_LEN;   // never 0: it is a divisor in many places

bool track_init() {
  if (segments) return true;
  const size_t bytes = sizeof(Segment) * MAX_SEGS;
  // ~48 KB touched ~200 times a frame: PSRAM through the cache is fine and
  // keeps the internal heap for the framebuffer and the textures.
  bool psram = true;
  segments = (Segment*)plat_alloc_psram(bytes);
  if (!segments) { psram = false; segments = (Segment*)plat_alloc_internal(bytes); }
  if (!segments) {
    plat_log("track: no memory for %u segments\n", (unsigned)MAX_SEGS);
    return false;
  }
  memset(segments, 0, bytes);
  plat_log("track: %u segments (%u B) in %s\n", (unsigned)MAX_SEGS, (unsigned)bytes,
           psram ? "PSRAM" : "internal SRAM");
  return true;
}

void track_clear() {
  segCount = 0;
}

float lastY() {
  return (segCount == 0) ? 0 : segments[segCount - 1].y;
}

int addSeg(float curve, float y) {
  if (!segments || segCount >= MAX_SEGS) return -1;
  Segment& s = segments[segCount];
  memset(&s, 0, sizeof(s));
  s.curve  = curve;
  s.y      = y;
  s.ground = groundPack(MAT_GRASS, MAT_GRASS);
  s.spr[0] = s.spr[1] = -1;
  return segCount++;
}

void addRoad(int enter, int hold, int leave, float curve, float hillY) {
  float sY  = lastY();
  float eY  = sY + hillY * SEG_LEN;
  int total = enter + hold + leave;
  if (total <= 0) return;
  for (int n = 0; n < enter; n++)
    addSeg(easeIn(0, curve, (float)n / enter), easeInOut(sY, eY, (float)(n + 1) / total));
  for (int n = 0; n < hold; n++)
    addSeg(curve, easeInOut(sY, eY, (float)(enter + n + 1) / total));
  for (int n = 0; n < leave; n++)
    addSeg(easeInOut(curve, 0, (float)n / leave),
           easeInOut(sY, eY, (float)(enter + hold + n + 1) / total));
}

void setSprite(int idx, int side, int type, float dist) {
  if (idx < 0 || idx >= segCount || side < 0 || side > 1) return;
  segments[idx].spr[side] = (int8_t)type;
  segments[idx].sprOff[side] = (uint8_t)iclamp((int)lroundf(fabsf(dist) * 16.0f), 0, 255);
}

void track_finish() {
  trackLength = (float)(segCount > 0 ? segCount : 1) * SEG_LEN;
}
