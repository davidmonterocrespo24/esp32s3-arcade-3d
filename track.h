/*
  ═══════════════════════════════════════════════════════════════
  TRACK STORAGE AND LOW-LEVEL BUILDERS
  The segment buffer (MAX_SEGS) is allocated once by track_init();
  circuits.cpp fills it with buildCircuit(). Every lookup wraps with
  segCount (never MAX_SEGS).
  ═══════════════════════════════════════════════════════════════
*/

#ifndef TRACK_H
#define TRACK_H

#include "structs.h"
#include "config.h"

// ═══════════════════════════════════════════════════════════════
//  GLOBAL TRACK STATE
// ═══════════════════════════════════════════════════════════════
extern Segment* segments;     // MAX_SEGS entries (PSRAM preferred)
extern int      segCount;     // segments in the current circuit
extern float    trackLength;  // segCount * SEG_LEN

// Allocate the segment buffer (once; PSRAM first, internal fallback,
// placement logged). Returns false when there is no memory at all.
bool track_init();

// Start a new circuit: segCount = 0.
void track_clear();

// Wrap any index into [0, segCount) (0 when the track is empty).
static inline int segWrap(int i) {
  if (segCount <= 0) return 0;
  i %= segCount;
  return i < 0 ? i + segCount : i;
}
static inline Segment& segAt(int i) { return segments[segWrap(i)]; }

// ═══════════════════════════════════════════════════════════════
//  BUILDERS
// ═══════════════════════════════════════════════════════════════
float lastY();
// Append a plain segment (grass both sides, no flags, no sprites).
// Returns its index, or -1 when the buffer is full.
int  addSeg(float curve, float y);
// Section with eased curve entry/exit and an eased elevation change of
// hillY * SEG_LEN spread over the whole section.
void addRoad(int enter, int hold, int leave, float curve, float hillY);
// Place a sprite on one side (0 left, 1 right) at |x| = dist half-widths.
void setSprite(int idx, int side, int type, float dist);
// Done building: trackLength = segCount * SEG_LEN.
void track_finish();

#endif // TRACK_H
