/*
  ═══════════════════════════════════════════════════════════════
  ONE CAR DRAWN AT A ROAD-PROJECTED POSITION (rivals)
  ═══════════════════════════════════════════════════════════════
*/

#ifndef RENDER_TRAFFIC_H
#define RENDER_TRAFFIC_H

#include <stdint.h>

// Pixels per model unit relative to the road projection scale. Chosen so a
// car at the player's distance is exactly the size of the player car.
#define TRAFFIC_PX_PER_SCALE 27000.0f

// (sx, sy): projected ground position, scale: road projection scale,
// yaw: heading relative to the line of sight, pitch: viewing elevation,
// camDistModel: distance from the eye in model units (perspective),
// paint: RGB565 (0 = texture paint), brake: brake lights on.
void drawTrafficCar(float sx, float sy, float scale, uint16_t paint, float fog,
                    float yaw, float pitch, bool inTunnel, float camDistModel, bool brake);

#endif // RENDER_TRAFFIC_H
