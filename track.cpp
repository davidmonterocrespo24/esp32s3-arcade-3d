/*
  ═══════════════════════════════════════════════════════════════
  TRACK GENERATION IMPLEMENTATION
  ═══════════════════════════════════════════════════════════════
*/

#include "track.h"
#include "utils.h"
#include "colors.h"
#include <Arduino.h>
#include <math.h>

// ═══════════════════════════════════════════════════════════════
//  GLOBAL TRACK VARIABLES (Definition)
// ═══════════════════════════════════════════════════════════════
Segment segments[TOTAL_SEGS];
int segCount = 0;
float trackLength;

TrafficCar trafficCars[MAX_CARS];

// ═══════════════════════════════════════════════════════════════
//  IMPLEMENTATION
// ═══════════════════════════════════════════════════════════════

float lastY() {
  return (segCount == 0) ? 0 : segments[segCount - 1].y;
}

void addSeg(float curve, float y, bool isTunnel) {
  if (segCount >= TOTAL_SEGS) return;
  Segment& s = segments[segCount++];
  s.curve       = curve;
  s.y           = y;
  s.spriteType  = -1;
  s.spriteOff16 = 0;
  s.tunnel      = isTunnel ? 1 : 0;
  s.lamp        = 0;
  s.buildL = s.buildR = 0;
  s.styleL = s.styleR = 0;
}

void addRoad(int enter, int hold, int leave, float curve, float hillY) {
  float sY  = lastY();
  float eY  = sY + hillY * SEG_LEN;
  int total = enter + hold + leave;
  for (int n = 0; n < enter; n++)
    addSeg(easeIn(0, curve, (float)n / enter), easeInOut(sY, eY, (float)n / total));
  for (int n = 0; n < hold; n++)
    addSeg(curve, easeInOut(sY, eY, (float)(enter + n) / total));
  for (int n = 0; n < leave; n++)
    addSeg(easeInOut(curve, 0, (float)n / leave),
           easeInOut(sY, eY, (float)(enter + hold + n) / total));
}

void addSprite(int idx, int type, float off) {
  if (idx >= 0 && idx < segCount) {
    segments[idx].spriteType  = (int8_t)type;
    segments[idx].spriteOff16 = (int8_t)iclamp((int)lroundf(off * 16.0f), -127, 127);
  }
}

static void markTunnel(int start, int len) {
  for (int i = start; i < start + len && i < TOTAL_SEGS; i++) {
    segments[i].tunnel = 1;
    segments[i].buildL = segments[i].buildR = 0;
    segments[i].spriteType = -1;
    segments[i].lamp = 0;
  }
}

static uint8_t randomStyle() {
  return (uint8_t)(random(0, BUILDING_PALETTE) | (random(0, 4) << 3));
}

static uint8_t randomHeight() {
  return (uint8_t)iclamp(random(BUILDING_H_MIN, BUILDING_H_MAX) / BUILDING_H_UNIT, 1, 255);
}

void buildTrack() {
  segCount = 0;

  // Start/finish straight
  addRoad(0, 14, 0, 0, 0);

#if RANDOM_TRACK
  // Random track: combines straights, curves, and hills/dips
  const int CLOSE_SEGS = 40;   // reserved to bring the elevation back to 0
  int pendingReturnDir = 0;
  float pendingReturnMag = 0.0f;
  while (segCount < TOTAL_SEGS - CLOSE_SEGS) {
    int enter = random(4, 10);
    int hold  = random(6, 22);
    int leave = random(4, 10);
    if (segCount + enter + hold + leave > TOTAL_SEGS - CLOSE_SEGS) break;

    float curve = (float)random(-80, 81) / 10.0f; // -8.0 to 8.0
    if (curve > -2.0f && curve < 2.0f) curve = 0.0f;

    float hill = 0.0f;
    if (pendingReturnDir != 0) {
      hill = (float)pendingReturnDir * pendingReturnMag;
      pendingReturnDir = 0;
    } else {
      float currentY = lastY();
      const float maxAllowedHill = 8.0f;
      if (fabsf(currentY) > SEG_LEN * 4) {
        hill = (currentY > 0) ? -maxAllowedHill : maxAllowedHill;   // force return
      } else {
        hill = (float)random(-12, 13);
        if (hill > -6.0f && hill < 6.0f) hill = 0.0f;
        if (hill != 0.0f) {
          pendingReturnDir = (hill > 0.0f) ? -1 : 1;
          pendingReturnMag = fmaxF(6.0f, fabsf(hill) * 0.6f);
        }
      }
    }
    addRoad(enter, hold, leave, curve, hill);
  }
#else
  // Varied fixed circuit, repeated to fill the longer track
  while (segCount < TOTAL_SEGS - 200) {
    addRoad(8, 12, 8, -6.0, 10);
    addRoad(5, 8, 5, 0, -15);
    addRoad(8, 12, 8, 7.0, 0);
    addRoad(5, 8, 5, 0, 20);
    addRoad(8, 12, 8, -5.5, -10);
    addRoad(5, 8, 5, 0, 0);
    addRoad(8, 12, 8, 6.5, 15);
    addRoad(5, 8, 5, 0, -20);
  }
#endif

  // Closing section: ease the elevation EXACTLY back to 0 over every
  // remaining segment. (The old code clamped the closing hill to +-14 and
  // then padded with y=0 segments, which left a step at the finish line
  // whenever the track ended higher than 2800 units.)
  {
    int remain = TOTAL_SEGS - segCount;
    float sY = lastY();
    for (int n = 0; n < remain; n++)
      addSeg(0, easeInOut(sY, 0, (float)(n + 1) / remain));
  }
  trackLength = (float)TOTAL_SEGS * SEG_LEN;

  // 1. TUNNELS: one long, one short
  markTunnel(TOTAL_SEGS / 3, 60);
  markTunnel((TOTAL_SEGS * 3) / 4, 40);

  // 2. CITY BLOCKS (left and right are independent)
  int counter[2] = { 0, 0 };
  uint8_t curH[2] = { 0, 0 }, curStyle[2] = { 0, 0 };
  for (int i = 0; i < TOTAL_SEGS; i++) {
    Segment& s = segments[i];
    if (s.tunnel) { counter[0] = counter[1] = 0; continue; }
    for (int side = 0; side < 2; side++) {
      if (counter[side] <= 0) {
        if (random(0, 10) < 6) {
          curH[side] = randomHeight();
          curStyle[side] = randomStyle();
          counter[side] = random(BUILDING_SEG_MIN, BUILDING_SEG_MAX);
          // Two different towers back to back read better than one long slab
          if (random(0, 3) == 0) counter[side] /= 2;
        } else {
          curH[side] = 0;
          counter[side] = random(BUILDING_GAP_MIN, BUILDING_GAP_MAX);
        }
      }
      if (side == 0) { s.buildL = curH[0]; s.styleL = curStyle[0]; }
      else           { s.buildR = curH[1]; s.styleR = curStyle[1]; }
      counter[side]--;
    }
  }

  // 3. SCENERY in the gaps: trees and bushes, roadside marker posts
  for (int n = 8; n < TOTAL_SEGS; n++) {
    Segment& s = segments[n];
    if (s.tunnel) continue;
    bool openL = s.buildL == 0, openR = s.buildR == 0;
    if (!openL && !openR) continue;
    int r = random(0, 100);
    float side = (openL && (!openR || random(0, 2))) ? -1.0f : 1.0f;
    if (r < 22) {
      int type = random(0, 3);   // pine, tree, bush
      addSprite(n, type, side * (1.6f + random(0, 120) / 100.0f));
    } else if (r < 26) {
      addSprite(n, SPR_ROCK, side * (1.5f + random(0, 80) / 100.0f));
    } else if ((n % 5) == 0) {
      addSprite(n, SPR_POST, side * 1.18f);
    }
  }

  // 4. STREET LAMPS along the city stretches (light pools at night)
  // One lamp every 9 segments, alternating sides (1 = left, 2 = right)
  int lampSide = 1;
  for (int n = 4; n < TOTAL_SEGS; n += 9) {
    Segment& s = segments[n];
    if (s.tunnel) continue;
    if (s.buildL || s.buildR) { s.lamp = (uint8_t)lampSide; lampSide = 3 - lampSide; }
  }
}

// Traffic colours in flash
static const uint16_t trafficColors[] = {
  0x051C,  // blue
  0xDDE0,  // yellow
  0xC618,  // silver
  0x05A0,  // green
  0xFC60,  // orange
  0xA01C,  // purple
  0x05BC,  // cyan
  0xB1E8,  // dark red
  0xFFFF,  // white
  0x6318,  // graphite
  0x0780,  // dark green
  0xC5E0   // gold
};

static float laneCentre(int lane) {
  return (lane - 1) * (2.0f / LANES);   // -0.67, 0, +0.67
}

void initTraffic(float maxSpeed) {
  for (int i = 0; i < MAX_CARS; i++) {
    TrafficCar& c = trafficCars[i];
    c.offset       = laneCentre(random(0, LANES));
    c.targetOffset = c.offset;
    // Spread evenly round the lap, with some jitter, never on the start grid
    c.z      = (float)((i + 1) * (TOTAL_SEGS / (MAX_CARS + 1)) + random(0, 8)) * SEG_LEN;
    c.speed  = maxSpeed * (0.35f + random(0, 40) / 100.0f);
    c.color  = trafficColors[i % 12];
  }
}
