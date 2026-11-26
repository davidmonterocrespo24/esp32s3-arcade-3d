/*
  ═══════════════════════════════════════════════════════════════
  RACE HUD
  Position, lap, race time, best lap, a lap progress bar with a dot
  per racer, a minimap, turbo meter and a segmented speedometer, on
  translucent panels.
  ═══════════════════════════════════════════════════════════════
*/

#include "render_hud.h"
#include "gfx.h"
#include "colors.h"
#include "physics.h"
#include "race.h"
#include "circuits.h"
#include "track.h"
#include "ui.h"
#include "utils.h"
#include <math.h>
#include <stdio.h>

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

// Fraction of the current lap driven (0 on the grid, behind the line)
static float lapFraction(const Racer& R) {
  if (R.progress <= 0 || !(trackLength > 0)) return 0.0f;
  return fmodf(R.progress, trackLength) / trackLength;
}

void drawHUD(float timeSec, float fps, float lcdFps, float frameMs) {
  char buf[48], tbuf[24];
  const Racer& P = racers[PLAYER_ID];
  const int laps = curCircuit().laps;

  // Position / lap panel
  gfx_fillRectMode(0, 0, 96, 44, COL_BLACK, GFX_BLEND, 15);
  gfx_fillRect(0, 44, 96, 1, COL_RED);
  snprintf(buf, sizeof(buf), "POS %d/%d", P.place, NUM_RACERS);
  gfx_textShadow(5, 4, buf, P.place == 1 ? COL_YELLOW : COL_WHITE, 2, COL_BLACK);
  snprintf(buf, sizeof(buf), "LAP %d/%d", imin(P.lap + 1, laps), laps);
  gfx_textShadow(5, 25, buf, COL_WHITE, 2, COL_BLACK);

  // Race time / best lap
  gfx_fillRectMode(SCR_W - 100, 0, 100, 26, COL_BLACK, GFX_BLEND, 15);
  ui_fmtTime(tbuf, sizeof(tbuf), P.finished ? P.finishTime : raceTime);
  snprintf(buf, sizeof(buf), "TIME %s", tbuf);
  gfx_textShadow(SCR_W - 94, 4, buf, COL_YELLOW, 1, COL_BLACK);
  if (P.bestLap > 0) ui_fmtTime(tbuf, sizeof(tbuf), P.bestLap);
  else snprintf(tbuf, sizeof(tbuf), "--:--.--");
  snprintf(buf, sizeof(buf), "BEST %s", tbuf);
  gfx_textShadow(SCR_W - 94, 15, buf, COL_GREEN, 1, COL_BLACK);

  // Lap progress bar with a dot per racer (player on top)
  {
    const int x0 = 108, x1 = SCR_W - 110, y = 6;
    const float w = (float)(x1 - x0);
    gfx_fillRectMode(x0 - 2, y - 2, x1 - x0 + 4, 7, COL_BLACK, GFX_BLEND, 14);
    float pp = lapFraction(P);
    gfx_fillRect(x0, y, (int)(w * pp), 3, rgb(240, 200, 40));
    for (int i = 0; i < NUM_RACERS; i++) {
      if (i == PLAYER_ID) continue;
      int dx = x0 + (int)(w * lapFraction(racers[i]));
      gfx_fillRect(dx - 1, y, 3, 3, racers[i].paint ? racers[i].paint : rgb(60, 90, 220));
    }
    gfx_fillRect(x0 + (int)(w * pp) - 1, y - 2, 3, 7, COL_WHITE);
  }

  // Minimap with the racers
  gfx_fillRectMode(2, SCR_H - 76, 62, 62, COL_BLACK, GFX_BLEND, 10);
  ui_drawMinimap(5, SCR_H - 73, 56, true);

  // Turbo meter
  {
    const int x = SCR_W - 94, y = SCR_H - 84, w = 60;
    gfx_fillRectMode(x - 2, y - 2, w + 4, 7, COL_BLACK, GFX_BLEND, 16);
    uint16_t c = boosting ? rgb(120, 200, 255) : rgb(40, 120, 230);
    gfx_fillRect(x, y, (int)(w * turboMeter), 3, c);
    gfx_text(x - 32, y - 2, "TURBO", rgb(150, 190, 255), 1);
  }

  if (autopilot && ((int)(timeSec * 2) & 1))
    gfx_textCentered(SCR_CX, 16, "AUTO", rgb(120, 220, 255), 1, COL_BLACK);

  drawSpeedometer(speed, maxSpeed);

#if SHOW_FPS
  snprintf(buf, sizeof(buf), "%2d FPS %4.1fMS LCD %2d", (int)(fps + 0.5f), frameMs, (int)(lcdFps + 0.5f));
  gfx_fillRectMode(0, SCR_H - 11, gfx_textWidth(buf, 1) + 6, 11, COL_BLACK, GFX_BLEND, 14);
  gfx_text(3, SCR_H - 9, buf, rgb(150, 255, 150), 1);
#else
  (void)fps; (void)lcdFps; (void)frameMs;
#endif
}
