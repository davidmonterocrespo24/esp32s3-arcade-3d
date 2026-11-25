/*
  ═══════════════════════════════════════════════════════════════
  HUD AND SPEEDOMETER RENDERING IMPLEMENTATION
  Translucent panels (alpha blending is free now that we own the
  framebuffer), a segmented arc speedometer and a lap progress bar.
  ═══════════════════════════════════════════════════════════════
*/

#include "render_hud.h"
#include "rendering.h"
#include "physics.h"
#include "track.h"
#include "utils.h"
#include <math.h>
#include <stdio.h>

static void fmtTime(char* out, int n, float t) {
  if (t < 0) t = 0;
  int mins = (int)t / 60;
  int secs = (int)t % 60;
  int cs = (int)((t - (int)t) * 100);
  snprintf(out, n, "%d:%02d.%02d", mins, secs, cs);
}

static void drawSpeedometer(float spd, float maxSpd) {
  const int cx = SCR_W - 44, cy = SCR_H - 38, r = 34;
  gfx_fillEllipse(cx, cy, r + 4, r + 4, COL_BLACK, GFX_BLEND, 16);
  int kmh = (int)(spd * 300.0f / maxSpd + 0.5f);

  // 28 ticks over 270 degrees, lit up to the current speed. Geometry and
  // colours are computed once (no trig per frame).
  const int TICKS = 28;
  static float tick[TICKS][8];
  static uint16_t tickCol[TICKS];
  static bool init = false;
  if (!init) {
    for (int i = 0; i < TICKS; i++) {
      float a = (-225.0f + 270.0f * i / (TICKS - 1)) * (float)PI / 180.0f;
      float ca = cosf(a), sa = sinf(a), c2 = cosf(a + 0.05f), s2 = sinf(a + 0.05f);
      float r0 = r - 7.0f, r1 = r - ((i % 3 == 0) ? 0.0f : 2.0f);
      float q[8] = { cx + ca * r0, cy + sa * r0, cx + ca * r1, cy + sa * r1,
                     cx + c2 * r1, cy + s2 * r1, cx + c2 * r0, cy + s2 * r0 };
      for (int k = 0; k < 8; k++) tick[i][k] = q[k];
      float f = (float)i / (TICKS - 1);
      tickCol[i] = f < 0.6f ? lerpCol(COL_GREEN, COL_YELLOW, f / 0.6f)
                            : lerpCol(COL_YELLOW, COL_RED, (f - 0.6f) / 0.4f);
    }
    init = true;
  }
  int lit = (int)(kmh / 300.0f * TICKS + 0.5f);
  for (int i = 0; i < TICKS; i++)
    gfx_fillPoly(tick[i], 4, i < lit ? tickCol[i] : rgb(60, 60, 70));

  char buf[8];
  snprintf(buf, sizeof(buf), "%d", kmh);
  uint16_t numCol = kmh > 250 ? COL_RED : (kmh > 200 ? COL_YELLOW : COL_WHITE);
  gfx_textCentered(cx, cy - 7, buf, numCol, 2, COL_BLACK);
  gfx_textCentered(cx, cy + 10, "KM/H", rgb(170, 170, 180), 1, COL_BLACK);
}

void drawHUD(float timeSec, float fps, float lcdFps, float frameMs) {
  char buf[48], tbuf[24];

  // Lap / time panel
  gfx_fillRectMode(0, 0, 104, 36, COL_BLACK, GFX_BLEND, 15);
  gfx_fillRect(0, 36, 104, 1, COL_RED);
  snprintf(buf, sizeof(buf), "LAP %d/%d", currentLap, totalLaps);
  gfx_textShadow(6, 4, buf, COL_WHITE, 2, COL_BLACK);
  fmtTime(tbuf, sizeof(tbuf), currentLapTime);
  snprintf(buf, sizeof(buf), "TIME %s", tbuf);
  gfx_textShadow(6, 24, buf, COL_YELLOW, 1, COL_BLACK);

  // Best / last lap
  if (bestLapTime > 0) {
    gfx_fillRectMode(SCR_W - 100, 0, 100, 26, COL_BLACK, GFX_BLEND, 15);
    fmtTime(tbuf, sizeof(tbuf), bestLapTime);
    snprintf(buf, sizeof(buf), "BEST %s", tbuf);
    gfx_textShadow(SCR_W - 94, 4, buf, COL_GREEN, 1, COL_BLACK);
    fmtTime(tbuf, sizeof(tbuf), lastLapTime);
    snprintf(buf, sizeof(buf), "LAST %s", tbuf);
    gfx_textShadow(SCR_W - 94, 15, buf, rgb(200, 200, 210), 1, COL_BLACK);
  }

  // Lap progress bar with traffic dots
  {
    const int x0 = 116, x1 = SCR_W - 108, y = 6;
    float pos = (position + playerZdist) / trackLength;
    pos -= floorf(pos);
    gfx_fillRectMode(x0 - 2, y - 2, x1 - x0 + 4, 7, COL_BLACK, GFX_BLEND, 14);
    gfx_fillRect(x0, y, (int)((x1 - x0) * pos), 3, rgb(240, 200, 40));
    for (int i = 0; i < MAX_CARS; i++) {
      float cp = trafficCars[i].z / trackLength;
      gfx_pixel(x0 + (int)((x1 - x0) * cp), y + 1, trafficCars[i].color);
    }
    gfx_fillRect(x0 + (int)((x1 - x0) * pos) - 1, y - 2, 3, 7, COL_WHITE);
  }

  if (autopilot && ((int)(timeSec * 2) & 1))
    gfx_textCentered(SCR_CX, 18, "AUTO", rgb(120, 220, 255), 1, COL_BLACK);

  drawSpeedometer(speed, maxSpeed);

#if SHOW_FPS
  snprintf(buf, sizeof(buf), "%2d FPS %4.1fMS LCD %2d", (int)(fps + 0.5f), frameMs, (int)(lcdFps + 0.5f));
  gfx_fillRectMode(0, SCR_H - 11, gfx_textWidth(buf, 1) + 6, 11, COL_BLACK, GFX_BLEND, 14);
  gfx_text(3, SCR_H - 9, buf, rgb(150, 255, 150), 1);
#else
  (void)fps; (void)lcdFps; (void)frameMs;
#endif
}
