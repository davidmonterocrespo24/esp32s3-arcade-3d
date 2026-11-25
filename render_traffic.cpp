/*
  ═══════════════════════════════════════════════════════════════
  TRAFFIC CAR RENDERING IMPLEMENTATION
  Traffic uses the same textured mesh as the player (repainted), seen
  from its real viewing angle. Far cars fall back to a few rectangles.
  ═══════════════════════════════════════════════════════════════
*/

#include "render_traffic.h"
#include "rendering.h"
#include "utils.h"
#include <math.h>

// Pixels per model unit relative to the road scale. Chosen so a traffic car
// at the player's distance is exactly the size of the player car.
#define TRAFFIC_PX_PER_SCALE 27000.0f

void drawTrafficCar(float sx, float sy, float scale, uint16_t color, float fog,
                    float yaw, float pitch, bool inTunnel, float camDistModel) {
  if (sy < 0 || sx < -60 || sx > SCR_W + 60) return;
  float ppu = scale * TRAFFIC_PX_PER_SCALE;
  float carW = 2.9f * ppu;
  bool lights = pal.night > 0.3f || inTunnel;
  uint16_t fogc = inTunnel ? COL_BLACK : pal.fog;

  if (carW < 9.0f) {
    // LOD: body, cabin and tail lights as rectangles
    float w = fmaxF(2.0f, carW), h = fmaxF(1.0f, carW * 0.34f);
    uint16_t body = blend565(fogc, darkenCol(color, 0.8f), (uint32_t)(fog * 32));
    uint16_t cab = blend565(fogc, rgb(30, 30, 40), (uint32_t)(fog * 32));
    gfx_fillQuad(sx - w * 0.5f, sy, sx + w * 0.5f, sy, sx + w * 0.5f, sy - h, sx - w * 0.5f, sy - h, body);
    gfx_fillQuad(sx - w * 0.32f, sy - h, sx + w * 0.32f, sy - h, sx + w * 0.26f, sy - h * 1.6f, sx - w * 0.26f, sy - h * 1.6f, cab);
    if (lights) {
      gfx_pixelMode((int)(sx - w * 0.38f), (int)(sy - h * 0.6f), COL_RED, GFX_ADD, 32);
      gfx_pixelMode((int)(sx + w * 0.38f), (int)(sy - h * 0.6f), COL_RED, GFX_ADD, 32);
    }
    return;
  }

  CarRender cr;
  cr.ax = sx;
  cr.ay = sy;
  cr.pxPerUnit = ppu;
  cr.yaw = yaw;
  cr.pitch = pitch;
  cr.roll = 0;
  cr.camDist = fmaxF(6.0f, camDistModel);
  cr.paint = color;
  cr.fog = inTunnel ? fminF(fog * 1.4f, 0.9f) : fog;
  cr.fogColor = fogc;
  cr.lightScale = inTunnel ? 0.75f : 1.0f;
  cr.tunnelLight = inTunnel;
  cr.lights = lights;
  cr.brake = false;
  cr.shadow = true;
  renderCar(cr);
}
