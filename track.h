/*
  ═══════════════════════════════════════════════════════════════
  TRACK GENERATION AND MANAGEMENT
  ═══════════════════════════════════════════════════════════════
*/

#ifndef TRACK_H
#define TRACK_H

#include "structs.h"
#include "config.h"

// Sprite types stored in Segment::spriteType
enum SpriteType { SPR_PINE = 0, SPR_TREE = 1, SPR_BUSH = 2, SPR_ROCK = 3, SPR_POST = 4 };

// ═══════════════════════════════════════════════════════════════
//  GLOBAL TRACK VARIABLES
// ═══════════════════════════════════════════════════════════════
extern Segment segments[TOTAL_SEGS];
extern int segCount;
extern float trackLength;

// ═══════════════════════════════════════════════════════════════
//  TRACK FUNCTIONS
// ═══════════════════════════════════════════════════════════════
float lastY();
void addSeg(float curve, float y, bool isTunnel = false);
void addRoad(int enter, int hold, int leave, float curve, float hillY);
void addSprite(int idx, int type, float off);
void buildTrack();

// ═══════════════════════════════════════════════════════════════
//  TRAFFIC MANAGEMENT
// ═══════════════════════════════════════════════════════════════
extern TrafficCar trafficCars[MAX_CARS];
void initTraffic(float maxSpeed);

#endif // TRACK_H
