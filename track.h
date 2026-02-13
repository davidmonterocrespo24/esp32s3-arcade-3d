/*
  ═══════════════════════════════════════════════════════════════
  TRACK GENERATION AND MANAGEMENT
  ═══════════════════════════════════════════════════════════════
*/

#ifndef TRACK_H
#define TRACK_H

#include "structs.h"
#include "config.h"

// ═══════════════════════════════════════════════════════════════
//  GLOBAL TRACK VARIABLES
// ═══════════════════════════════════════════════════════════════
extern Segment segments[TOTAL_SEGS];
extern int segCount;
extern float trackLength;

// ═══════════════════════════════════════════════════════════════
//  TRACK FUNCTIONS
// ═══════════════════════════════════════════════════════════════

// Get the height of the last segment
float lastY();

// Add a segment to the track
void addSeg(float curve, float y, bool isTunnel = false);

// Add a road section with curves and elevation
void addRoad(int enter, int hold, int leave, float curve, float hillY);

// Add a sprite to a specific segment
void addSprite(int idx, int type, float off);

// Build the complete track
void buildTrack();

// ═══════════════════════════════════════════════════════════════
//  TRAFFIC MANAGEMENT (unused in race mode, MAX_CARS=0)
// ═══════════════════════════════════════════════════════════════
#if MAX_CARS > 0
extern TrafficCar trafficCars[MAX_CARS];
void initTraffic(float maxSpeed);
#endif

// ═══════════════════════════════════════════════════════════════
//  COMPETITOR MANAGEMENT
// ═══════════════════════════════════════════════════════════════
extern CompetitorCar competitors[NUM_COMPETITORS];

// Initialize race competitors at the starting grid
void initCompetitors(float maxSpeed);

#endif // TRACK_H
