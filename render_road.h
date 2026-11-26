/*
  ═══════════════════════════════════════════════════════════════
  ROAD PASS: road, ground materials, tunnels, buildings, rails,
  scenery, lamps and cars (one back-to-front pass)
  ═══════════════════════════════════════════════════════════════
*/

#ifndef RENDER_ROAD_H
#define RENDER_ROAD_H

#include <stdint.h>
#include "config.h"
#include "structs.h"

// A projected road point: centre x, y, half-width (px), scale, camera z
struct ProjPt { float x, y, w, scale, z; };

// Shared camera/projection for the frame (so other modules project exactly
// like the road). Valid from the start of drawRoad() until the next frame.
struct RoadCam { float camX, camY, camDepth; };
extern RoadCam roadCam;

#define ROAD_NEAR_Z 30.0f    // near plane (camera-space z, world units)

static inline void roadProject(ProjPt& p, float wx, float wy, float wz) {
  p.z = wz;
  p.scale = roadCam.camDepth / wz;
  p.x = SCR_CX + p.scale * (wx - roadCam.camX) * SCR_CX;
  p.y = SCR_CY - p.scale * (wy - roadCam.camY) * SCR_CY;
  p.w = p.scale * ROAD_W * SCR_CX;
}

// Per-frame boundary data, valid during and after drawRoad: world x offset
// of the road centre / elevation / camera z of boundary n (boundary n is the
// near edge of segment roadBaseIdx + n).
extern float roadBX[DRAW_DIST + 1], roadBY[DRAW_DIST + 1], roadBZ[DRAW_DIST + 1];
extern int   roadBaseIdx;        // segment index of boundary 0
extern float roadPosOff;         // camera position inside the base segment

// Draws the whole road pass for the frame. Calls cars_prepare(position)
// after pass 1 and cars_drawSlice() per segment in pass 2 (render_cars.h).
void drawRoad(float position, float playerX, float playerZdist, float cameraDepth);

#endif // RENDER_ROAD_H
