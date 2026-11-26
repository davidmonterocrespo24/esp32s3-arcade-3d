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

// ═══════════════════════════════════════════════════════════════
//  ADDITIONS (feature phase)
// ═══════════════════════════════════════════════════════════════
// Level of detail of a car view (render_cars picks it with hysteresis)
#define CAR_VIEW_FULL   0      // full mesh renderer
#define CAR_VIEW_MID    1      // mesh, cheaper lighting (CAR_LOD_MID)
#define CAR_VIEW_SPRITE 2      // a few shaded quads (far away)

// Everything needed to draw one car (filled by render_cars).
struct CarView {
  float sx, sy;          // screen anchor: the car's ground centre
  float ppu;             // pixels per model unit at the anchor
  float camDist;         // eye -> anchor distance in model units
  float yaw, pitch, roll;
  float fog;             // 0..1 towards the fog colour (black in tunnels)
  float flame;           // turbo flame strength 0..1
  uint16_t paint;        // RGB565, 0 = texture paint
  bool  tunnel;          // tunnel lighting and black fog
  bool  lights;          // tail lights
  bool  brake;           // brake lights
  uint8_t lod;           // CAR_VIEW_*
};
void drawCarView(const CarView& v, float timeSec);

// Car width in pixels below which a car is a sprite / not fully lit
// (render_cars adds hysteresis around these).
#ifndef CAR_SPRITE_W
#define CAR_SPRITE_W   10.0f
#endif
#ifndef CAR_MIDLOD_W
#define CAR_MIDLOD_W   40.0f
#endif

#endif // RENDER_TRAFFIC_H
