/*
  ═══════════════════════════════════════════════════════════════
  ONE CAR AT A ROAD-PROJECTED POSITION — implementation
  Rivals use the same textured mesh as the player (repainted). Small
  cars use the cheaper mesh lighting, far cars a few shaded quads.
  ═══════════════════════════════════════════════════════════════
*/

#include "render_traffic.h"
#include "render_player.h"
#include "gfx.h"
#include "colors.h"
#include "utils.h"
#include <math.h>

// Far LOD: shadow, body, glass band, roof, tail lights. Drawn about the
// same width / height as the mesh would be at this size.
static void drawSprite(const CarView& v) {
  const uint16_t fogc = v.tunnel ? COL_BLACK : pal.fog;
  const uint32_t fa = (uint32_t)(v.fog * 32.0f + 0.5f);
  const float w = fmaxF(2.0f, 2.9f * v.ppu);
  const float h = fmaxF(1.5f, w * 0.30f);
  const float sx = v.sx, sy = v.sy;
  const uint16_t paint = v.paint ? v.paint : rgb(40, 52, 110);
  const float light = v.tunnel ? 0.55f : clampF(0.45f + 0.5f * (pal.ambR + pal.lightR) * 0.5f, 0.35f, 1.0f);
  uint16_t body = blend565(fogc, darkenCol(paint, light), fa);
  uint16_t dark = blend565(fogc, darkenCol(paint, light * 0.55f), fa);
  uint16_t glass = blend565(fogc, rgb(24, 26, 34), fa);
  // contact shadow
  if (v.fog < 0.8f)
    gfx_fillEllipse((int)sx, (int)(sy + 0.5f), (int)(w * 0.62f), (int)fmaxF(1.0f, h * 0.35f), COL_BLACK, GFX_BLEND,
                    (int)(10 * (1.0f - v.fog)));
  // lower body (darker band at the bottom), cabin with the rear window
  gfx_fillQuad(sx - w * 0.5f, sy, sx + w * 0.5f, sy, sx + w * 0.5f, sy - h * 0.45f, sx - w * 0.5f, sy - h * 0.45f, dark);
  gfx_fillQuad(sx - w * 0.5f, sy - h * 0.45f, sx + w * 0.5f, sy - h * 0.45f, sx + w * 0.48f, sy - h, sx - w * 0.48f, sy - h, body);
  gfx_fillQuad(sx - w * 0.36f, sy - h, sx + w * 0.36f, sy - h, sx + w * 0.30f, sy - h * 1.55f, sx - w * 0.30f, sy - h * 1.55f, glass);
  gfx_fillQuad(sx - w * 0.30f, sy - h * 1.55f, sx + w * 0.30f, sy - h * 1.55f, sx + w * 0.28f, sy - h * 1.7f, sx - w * 0.28f, sy - h * 1.7f, body);
  if (v.lights || v.brake) {
    int a = (int)((v.brake ? 32 : 22) * (1.0f - v.fog * 0.6f));
    int ly = (int)(sy - h * 0.7f);
    gfx_pixelMode((int)(sx - w * 0.38f), ly, COL_RED, GFX_ADD, a);
    gfx_pixelMode((int)(sx + w * 0.38f), ly, COL_RED, GFX_ADD, a);
    if (w > 6) {
      gfx_glow((int)(sx - w * 0.38f), ly, 2, COL_RED, a / 2);
      gfx_glow((int)(sx + w * 0.38f), ly, 2, COL_RED, a / 2);
    }
  }
  if (v.flame > 0.05f && w > 4) {
    gfx_glow((int)(sx - w * 0.2f), (int)(sy - h * 0.2f), imax(1, (int)(w * 0.12f)), rgb(80, 130, 255), (int)(24 * v.flame));
    gfx_glow((int)(sx + w * 0.2f), (int)(sy - h * 0.2f), imax(1, (int)(w * 0.12f)), rgb(80, 130, 255), (int)(24 * v.flame));
  }
}

void drawCarView(const CarView& v, float timeSec) {
  if (v.ppu <= 0) return;
  if (v.lod >= CAR_VIEW_SPRITE) { drawSprite(v); return; }
  CarRender cr;
  cr.ax = v.sx;
  cr.ay = v.sy;
  cr.pxPerUnit = v.ppu;
  cr.yaw = v.yaw;
  cr.pitch = v.pitch;
  cr.roll = v.roll;
  cr.camDist = fmaxF(4.0f, v.camDist);
  cr.paint = v.paint;
  cr.fog = v.tunnel ? fminF(v.fog * 1.4f, 0.9f) : v.fog;
  cr.fogColor = v.tunnel ? COL_BLACK : pal.fog;
  cr.lightScale = v.tunnel ? 0.75f : 1.0f;
  cr.tunnelLight = v.tunnel;
  cr.lights = v.lights;
  cr.brake = v.brake;
  cr.shadow = true;
  renderCarLod(cr, v.lod == CAR_VIEW_FULL ? CAR_LOD_FULL : CAR_LOD_MID);
  if (v.flame > 0.02f) renderCarFlame(cr, v.flame, timeSec);
}

void drawTrafficCar(float sx, float sy, float scale, uint16_t paint, float fog,
                    float yaw, float pitch, bool inTunnel, float camDistModel, bool brake) {
  if (sy < 0 || sx < -60 || sx > SCR_W + 60) return;
  CarView v;
  v.sx = sx;
  v.sy = sy;
  v.ppu = scale * TRAFFIC_PX_PER_SCALE;
  v.camDist = fmaxF(6.0f, camDistModel);
  v.yaw = yaw;
  v.pitch = pitch;
  v.roll = 0;
  v.fog = fog;
  v.flame = 0;
  v.paint = paint;
  v.tunnel = inTunnel;
  v.lights = pal.night > 0.3f || inTunnel;
  v.brake = brake;
  float w = 2.9f * v.ppu;
  v.lod = w < CAR_SPRITE_W - 1.0f ? CAR_VIEW_SPRITE : (w < CAR_MIDLOD_W ? CAR_VIEW_MID : CAR_VIEW_FULL);
  drawCarView(v, 0.0f);
}
