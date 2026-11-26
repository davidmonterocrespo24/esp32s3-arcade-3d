/*
  ═══════════════════════════════════════════════════════════════
  RACE HUD
  Sized for a 2.8-inch 320x240 panel: the numbers that matter are
  big (position, speed), the rest is outlined 5x7 text on small
  translucent panels at the corners, leaving the centre to the road.

    top left     POS n/6 (big), LAP n/3, gap to the car ahead, and a
                 green / red banner when a position is gained / lost
    top right    race time, last lap, best lap
    bottom left  minimap with the racers in their paint colours
    bottom right speedometer dial, turbo meter
    bottom line  FPS / frame ms / LCD fps (SHOW_FPS)
  ═══════════════════════════════════════════════════════════════
*/

#pragma GCC optimize("O2")

#include "render_hud.h"
#include "gfx.h"
#include "colors.h"
#include "physics.h"
#include "race.h"
#include "circuits.h"
#include "track.h"
#include "ui.h"
#include "game.h"
#include "utils.h"
#include <math.h>
#include <stdio.h>

// Position / gap tracking (per race)
static int   sShown = 0;          // place the banner logic last reported
static int   sCand = 0;           // candidate place (must hold for a moment)
static float sCandT = 0;
static float sBannerT = -100;     // timeSec when the position banner started
static int   sBannerDelta = 0;    // + gained, - lost
static int   sBannerPlace = 0;
static float sGap = 0;            // smoothed gap to the car ahead (s)
static int   sGapTo = -1;
static float sLastT = -1;

void hud_reset() {
  sShown = 0;
  sCand = 0;
  sCandT = 0;
  sBannerT = -100;
  sBannerDelta = 0;
  sGap = 0;
  sGapTo = -1;
  sLastT = -1;
}

// Text on a shaded panel: no outline needed (one pass instead of two)
static void plain(int x, int y, const char* s, uint16_t c, int scale, int align = UI_LEFT) {
  UiTextStyle st = ui_style((float)scale, c);
  st.outlinePx = 0;
  ui_text((float)x, (float)y, s, st, align);
}

static const char* ordinal(int p) {
  return p == 1 ? "ST" : p == 2 ? "ND" : p == 3 ? "RD" : "TH";
}

// ═══════════════════════════════════════════════════════════════
//  SPEEDOMETER: 28 ticks over 270 degrees around a big number
// ═══════════════════════════════════════════════════════════════
#define TICKS 28
static float sTickCos[TICKS + 1], sTickSin[TICKS + 1];   // tick edges
static uint16_t sTickCol[TICKS];
static bool sTicksInit = false;

static void initTicks() {
  for (int i = 0; i <= TICKS; i++) {
    float a = (-225.0f + 270.0f * i / TICKS) * (float)PI / 180.0f;
    sTickCos[i] = cosf(a);
    sTickSin[i] = sinf(a);
  }
  for (int i = 0; i < TICKS; i++) {
    float f = (float)i / (TICKS - 1);
    sTickCol[i] = f < 0.55f ? lerpCol(UI_ACCENT2, UI_GOOD, f / 0.55f)
                : f < 0.8f  ? lerpCol(UI_GOOD, UI_GOLD, (f - 0.55f) / 0.25f)
                            : lerpCol(UI_GOLD, UI_BAD, (f - 0.8f) / 0.2f);
  }
  sTicksInit = true;
}

static void drawSpeedometer(int cx, int cy, float spd, float maxSpd) {
  if (!sTicksInit) initTicks();
  const float R = 35.0f;
  ui_shadeEllipse(cx, cy, 39, 20);
  gfx_circle(cx, cy, 39, UI_EDGE);
  int kmh = maxSpd > 0 ? (int)(spd * 300.0f / maxSpd + 0.5f) : 0;
  if (kmh < 0) kmh = 0;
  int lit = (int)(kmh / 300.0f * TICKS + 0.5f);
  for (int i = 0; i < TICKS; i++) {
    const bool major = (i % 4) == 0;
    const float r0 = R - (major ? 8.0f : 6.0f), r1 = R;
    // leave a small gap between ticks: use 70 % of the angular step
    const float c0 = sTickCos[i], s0 = sTickSin[i];
    const float c1 = c0 + (sTickCos[i + 1] - c0) * 0.7f, s1 = s0 + (sTickSin[i + 1] - s0) * 0.7f;
    float q[8] = { cx + c0 * r0, cy + s0 * r0, cx + c0 * r1, cy + s0 * r1,
                   cx + c1 * r1, cy + s1 * r1, cx + c1 * r0, cy + s1 * r0 };
    gfx_fillPoly(q, 4, i < lit ? sTickCol[i] : UI_RGB(52, 56, 80));
  }
  // needle tip marker at the current speed
  if (lit > 0) {
    int k = imin(lit, TICKS);
    float c = sTickCos[k], s = sTickSin[k];
    gfx_fillTri(cx + c * (R + 3), cy + s * (R + 3), cx + c * (R - 10) - s * 2.5f, cy + s * (R - 10) + c * 2.5f,
                cx + c * (R - 10) + s * 2.5f, cy + s * (R - 10) - c * 2.5f, COL_WHITE);
  }
  char buf[12];
  snprintf(buf, sizeof(buf), "%d", imin(kmh, 999));
  UiTextStyle st = ui_styleGrad(3.0f, COL_WHITE, kmh > 260 ? UI_BAD : UI_SILVER);
  st.slant = 0.12f;
  st.shadowDx = st.shadowDy = 0;
  ui_text((float)cx, (float)(cy - 11), buf, st, UI_CENTER);
  plain(cx, cy + 14, "KM/H", UI_DIM, 1, UI_CENTER);
}

// Turbo: 8 segments; glow while boosting, the label pulses when full
static void drawTurbo(int x, int y, float timeSec) {
  const int SEG = 8, sw = 5, gap = 1;
  float m = clampF(turboMeter, 0, 1);
  int lit = (int)(m * SEG + 0.999f);
  bool full = m > 0.995f;
  uint16_t lc = boosting ? COL_WHITE : full ? lerpCol(UI_ACCENT2, COL_WHITE, 0.5f + 0.5f * sinf(timeSec * 6.0f))
                                            : UI_DIM;
  ui_label(x, y, "TURBO", lc, 1, UI_LEFT);
  int bx = x + 33;
  gfx_fillRect(bx - 1, y - 1, SEG * (sw + gap) + 1, 9, UI_SHADOW);
  for (int i = 0; i < SEG; i++) {
    uint16_t c = i < lit ? (boosting ? UI_RGB(170, 236, 255) : UI_ACCENT2) : UI_RGB(30, 40, 64);
    if (i == lit - 1 && !full) {   // partial last segment: dimmer
      float part = m * SEG - (lit - 1);
      c = lerpCol(UI_RGB(30, 40, 64), c, clampF(part, 0, 1));
    }
    gfx_fillRect(bx + i * (sw + gap), y, sw, 7, c);
  }
  if (boosting) gfx_glow(bx + SEG * (sw + gap) / 2, y + 3, 26, UI_ACCENT2, 10);
}

// ═══════════════════════════════════════════════════════════════
//  POSITION, LAP, GAP
// ═══════════════════════════════════════════════════════════════
static void trackPosition(const Racer& P, float timeSec, float dt) {
  if (!raceRunning || P.finished) { sShown = P.place; sCand = P.place; sCandT = 0; return; }
  if (P.place != sCand) { sCand = P.place; sCandT = 0; }
  else sCandT += dt;
  if (sShown <= 0) sShown = P.place;
  if (sCandT > 0.3f && sCand != sShown) {
    sBannerDelta = sShown - sCand;
    sBannerPlace = sCand;
    sBannerT = timeSec;
    sShown = sCand;
  }
}

// Seconds between the player and the car ahead (or the car behind when
// leading), from the distance at the player's pace
static void updateGap(const Racer& P, float dt) {
  int other = P.place > 1 ? race_order(P.place - 1) : race_order(2);
  if (other == PLAYER_ID) return;
  const Racer& O = racers[other];
  float d = fabsf(O.progress - P.progress);
  float pace = fmaxF(fmaxF(P.speed, O.speed), maxSpeed * 0.35f);
  float g = d / pace;
  if (other != sGapTo) { sGap = g; sGapTo = other; }
  else sGap += (g - sGap) * clampF(dt * 4.0f, 0, 1);
}

static void drawPositionBlock(const Racer& P, int laps, float timeSec) {
  const int x = 2, y = 2, w = 112, h = 50;
  ui_panel(x, y, w, h, UI_ACCENT, 20);
  plain(x + 7, y + 5, "POS", UI_DIM, 1);
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", iclamp(P.place, 1, 9));
  UiTextStyle big = P.place == 1 ? ui_styleGold(4.0f) : ui_styleGrad(4.0f, COL_WHITE, UI_SILVER);
  big.slant = 0.14f;
  big.shadowDx = big.shadowDy = 0;
  // a short pop when the place changes
  float since = timeSec - sBannerT;
  if (since >= 0 && since < 0.25f) big.scale = 4.0f + 1.2f * (1.0f - since / 0.25f);
  ui_text((float)(x + 8), (float)(y + 17) - (big.scale - 4.0f) * 3.5f, buf, big, UI_LEFT);
  snprintf(buf, sizeof(buf), "/%d", NUM_RACERS);
  plain(x + 34, y + 31, buf, UI_DIM, 2);

  // lap
  gfx_vspan(x + 62, y + 6, y + h - 6, UI_EDGE);
  plain(x + 70, y + 5, "LAP", UI_DIM, 1);
  int lap = imin(P.lap + 1, laps);
  snprintf(buf, sizeof(buf), "%d", lap);
  UiTextStyle ls = ui_styleGrad(3.0f, COL_WHITE, UI_SILVER);
  bool finalLap = lap == laps && raceRunning && !P.finished;
  if (finalLap) ls = ui_styleGold(3.0f);
  ls.shadowDx = ls.shadowDy = 0;
  ui_text((float)(x + 70), (float)(y + 18), buf, ls, UI_LEFT);
  snprintf(buf, sizeof(buf), "/%d", laps);
  plain(x + 87, y + 31, buf, UI_DIM, 2);
}

static void drawUnderBlock(const Racer& P, float timeSec) {
  const int x = 4, y = 56;
  float since = timeSec - sBannerT;
  if (since >= 0 && since < 2.0f) {
    // position gained / lost
    float in = clampF(since / 0.15f, 0, 1), out = clampF((since - 1.7f) / 0.3f, 0, 1);
    int slide = (int)((1.0f - in) * -110.0f - out * 110.0f);
    bool up = sBannerDelta > 0;
    uint16_t c = up ? UI_GOOD : UI_BAD;
    char buf[24];
    snprintf(buf, sizeof(buf), "%s %d%s", up ? "UP TO" : "DOWN TO", sBannerPlace, ordinal(sBannerPlace));
    int w = ui_textWidth(buf, 1) + 24;
    gfx_fillRect(x + slide, y, w, 13, UI_SHADOW);
    gfx_fillRect(x + slide + 1, y + 1, w - 2, 11, darkenCol(c, 0.35f));
    gfx_fillRect(x + slide + 1, y + 1, 3, 11, c);
    ui_arrow(x + slide + 11, y + 6, 4, up ? 0 : 1, c);
    ui_label(x + slide + 19, y + 3, buf, COL_WHITE, 1);
    return;
  }
  if (!raceRunning || P.finished || raceTime < 2.0f || sGapTo < 0) return;
  // gap to the car ahead (or lead over the car behind)
  char buf[24];
  int cs = (int)(sGap * 10.0f + 0.5f);
  if (cs > 999) cs = 999;
  bool lead = P.place == 1;
  // behind: the car ahead and how far ahead it is; leading: the margin
  if (lead) snprintf(buf, sizeof(buf), "LEAD %d.%d", cs / 10, cs % 10);
  else snprintf(buf, sizeof(buf), "%s +%d.%d", racers[sGapTo].name, cs / 10, cs % 10);
  int w = ui_textWidth(buf, 1) + 20;
  ui_shadeRect(x, y, w, 13, 20);
  uint16_t sw = racers[sGapTo].paint ? racers[sGapTo].paint : UI_RGB(46, 62, 140);
  gfx_fillRect(x + 3, y + 3, 7, 7, UI_SHADOW);
  gfx_fillRect(x + 4, y + 4, 5, 5, sw);
  plain(x + 14, y + 3, buf, lead ? UI_GOLD : UI_TEXT, 1);
}

static void drawTimeBlock(const Racer& P) {
  const int w = 112, x = SCR_W - w - 2, y = 2, h = 50;
  ui_panel(x, y, w, h, 0, 20);
  char tb[16];
  plain(x + 8, y + 5, "TIME", UI_DIM, 1);
  ui_fmtTime(tb, sizeof(tb), P.finished ? P.finishTime : raceTime);
  UiTextStyle ts = ui_style(2.0f, COL_WHITE);
  ts.g[2] = ts.g[3] = UI_SILVER;
  ts.outlinePx = 0;
  ui_text((float)(x + w - 7), (float)(y + 15), tb, ts, UI_RIGHT);
  plain(x + 8, y + 33, "LAST", UI_DIM, 1);
  if (P.lastLap > 0) ui_fmtTime(tb, sizeof(tb), P.lastLap);
  else snprintf(tb, sizeof(tb), "-:--.--");
  plain(x + w - 7, y + 33, tb, UI_TEXT, 1, UI_RIGHT);
  plain(x + 8, y + 42, "BEST", UI_DIM, 1);
  if (P.bestLap > 0) ui_fmtTime(tb, sizeof(tb), P.bestLap);
  else snprintf(tb, sizeof(tb), "-:--.--");
  plain(x + w - 7, y + 42, tb, UI_GOLD, 1, UI_RIGHT);
}

void drawHUD(float timeSec, float fps, float lcdFps, float frameMs) {
  const Racer& P = racers[PLAYER_ID];
  const int laps = curCircuit().laps;
  float dt = (sLastT < 0) ? 0.0f : clampF(timeSec - sLastT, 0, 0.1f);
  sLastT = timeSec;
  trackPosition(P, timeSec, dt);
  updateGap(P, dt);

  drawPositionBlock(P, laps, timeSec);
  drawUnderBlock(P, timeSec);
  drawTimeBlock(P);

  // minimap, bottom left
  const int ms = 64, mx = 4, my = SCR_H - ms - 16;
  ui_shadeRect(mx - 2, my - 2, ms + 4, ms + 4, 14);
  ui_drawMinimap(mx, my, ms, true);

  // speed + turbo, bottom right
  drawTurbo(SCR_W - 86, SCR_H - 86, timeSec);
  drawSpeedometer(SCR_W - 44, SCR_H - 42, speed, maxSpeed);

  if (autopilot && game_state() != GS_DEMO) {   // the AI drives (finish cool-down, --auto): top centre
    int a = 20 + (int)(12.0f * sinf(timeSec * 5.0f));
    ui_shadeRect(SCR_CX - 20, 4, 40, 13, 20);
    gfx_hspan(4, SCR_CX - 20, SCR_CX + 20, UI_ACCENT2);
    UiTextStyle s = ui_style(1, UI_ACCENT2);
    s.alpha = (uint8_t)iclamp(a, 0, 32);
    ui_text((float)SCR_CX, 7.0f, "AUTO", s, UI_CENTER);
  }

#if SHOW_FPS
  char buf[48];
  snprintf(buf, sizeof(buf), "%2d FPS %4.1fMS LCD %2d", (int)(fps + 0.5f), frameMs, (int)(lcdFps + 0.5f));
  ui_shadeRect(0, SCR_H - 11, gfx_textWidth(buf, 1) + 6, 11, 16);
  gfx_text(3, SCR_H - 9, buf, UI_RGB(150, 255, 150), 1);
#else
  (void)fps; (void)lcdFps; (void)frameMs;
#endif
}
