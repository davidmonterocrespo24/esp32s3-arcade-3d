/*
  ═══════════════════════════════════════════════════════════════
  BUILDING RENDERING
  ═══════════════════════════════════════════════════════════════
*/

#ifndef RENDER_BUILDING_H
#define RENDER_BUILDING_H

#include <stdint.h>
#include "render_road.h"

// One segment-long slice of a building beside the road.
//   pn / pf    : projected road centre at the near / far end of the segment
//   heightUnits: Segment::buildL/R (height / BUILDING_H_UNIT)
//   style      : Segment::styleL/R (bits 0-2 facade, bits 3-4 windows)
//   showFront  : first slice of this building -> draw its front facade
//   fog        : 0..1 haze towards pal.fog
void drawBuilding(const ProjPt& pn, const ProjPt& pf, int heightUnits, uint8_t style,
                  int sIdx, bool isLeft, bool showFront, float fog);

#endif // RENDER_BUILDING_H
