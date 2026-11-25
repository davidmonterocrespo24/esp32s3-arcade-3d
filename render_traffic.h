/*
  ═══════════════════════════════════════════════════════════════
  TRAFFIC CAR RENDERING
  ═══════════════════════════════════════════════════════════════
*/

#ifndef RENDER_TRAFFIC_H
#define RENDER_TRAFFIC_H

#include <stdint.h>

// (sx, sy): projected ground position, scale: road projection scale,
// yaw: heading relative to the line of sight, pitch: viewing elevation,
// camDistModel: distance from the eye in model units (perspective).
void drawTrafficCar(float sx, float sy, float scale, uint16_t color, float fog,
                    float yaw, float pitch, bool inTunnel, float camDistModel);

#endif // RENDER_TRAFFIC_H
