/*
  ═══════════════════════════════════════════════════════════════
  UI SCREENS AND OVERLAYS — implementation (basic versions)
  ═══════════════════════════════════════════════════════════════
*/

#include "ui.h"
#include "gfx.h"
#include "colors.h"
#include "rendering.h"
#include "render_player.h"
#include "circuits.h"
#include "save.h"
#include "race.h"
#include "config.h"
#include "utils.h"
#include <math.h>
#include <stdio.h>

static const uint16_t UI_ACCENT = 0xF8A3;   // red
static const uint16_t UI_SEL    = 0xFEA0;   // yellow-orange

void ui_fmtTime(char* out, int n, float t) {
  if (!(t > 0)) t = 0;
  if (t > 5999) t = 5999;
  int cs = (int)(t * 100.0f + 0.5f);
  snprintf(out, n, "%d:%02d.%02d", cs / 6000, (cs / 100) % 60, cs % 100);
}

static void panel(int x, int y, int w, int h) {
  gfx_fillRectMode(x, y, w, h, COL_BLACK, GFX_BLEND, 20);
  gfx_fillRect(x, y, w, 2, UI_ACCENT);
  gfx_fillRect(x, y + h - 2, w, 2, UI_ACCENT);
}

// ═══════════════════════════════════════════════════════════════
//  SHOWROOM (title + menu backdrop)
// ═══════════════════════════════════════════════════════════════
static void showroom(float t, float carX, float ppu) {
  drawSky(t * 12.0f, t);
  for (int y = SCR_CY; y < SCR_H; y++) {
    float k = (float)(y - SCR_CY) / (SCR_H - SCR_CY);
    gfx_ditherRow(y, 0, SCR_W, lerpCol(pal.skylineNear, COL_BLACK, 0.3f), rgb(8, 8, 14), k);
  }
  int cx = (int)carX;
  gfx_fillEllipse(cx, 186, 120, 26, pal.sunGlow, GFX_ADD, 3);
  gfx_fillEllipse(cx, 186, 92, 19, rgb(40, 40, 52), GFX_SOLID, 32);
  gfx_fillEllipse(cx, 186, 88, 17, rgb(64, 64, 80), GFX_SOLID, 32);

  CarRender cr;
  cr.ax = carX; cr.ay = 188;
  cr.pxPerUnit = ppu;
  cr.yaw = t * 1.1f + 2.4f;
  cr.pitch = 0.32f;
  cr.roll = 0;
  cr.camDist = 14.0f;
  cr.paint = PLAYER_PAINT;
  cr.fog = 0; cr.fogColor = 0;
  cr.lightScale = 1.0f;
  cr.tunnelLight = false;
  cr.lights = true;
  cr.brake = false;
  cr.shadow = true;
  renderCar(cr);
}

void ui_drawTitle(float t) {
  showroom(t, SCR_CX, 17.0f);
  gfx_fillRectMode(0, 14, SCR_W, 58, COL_BLACK, GFX_BLEND, 14);
  gfx_fillRect(0, 16, SCR_W, 2, COL_RED);
  gfx_fillRect(0, 69, SCR_W, 2, COL_RED);
  gfx_textCentered(SCR_CX, 22, "ARCADE 3D", COL_WHITE, 4, rgb(150, 0, 0));
  gfx_textCentered(SCR_CX, 56, "ESP32-S3 PSEUDO-3D RACER", COL_YELLOW, 1, COL_BLACK);
  if (((int)(t * 2.0f)) & 1)
    gfx_textCentered(SCR_CX, 218, "PRESS START", COL_WHITE, 2, COL_BLACK);
}

void ui_drawMenuBackdrop(float t) {
  showroom(t, SCR_CX, 15.0f);
}

// ═══════════════════════════════════════════════════════════════
//  MENUS
// ═══════════════════════════════════════════════════════════════
void ui_drawMenu(const char* title, const char* const* items, int count, int sel, float t) {
  const int rowH = 22, w = 260;
  int h = 44 + count * rowH + 16;
  int x = (SCR_W - w) / 2, y = 24;
  panel(x, y, w, h);
  gfx_textCentered(SCR_CX, y + 10, title, COL_WHITE, 2, rgb(120, 0, 0));
  for (int i = 0; i < count; i++) {
    int iy = y + 44 + i * rowH;
    bool s = i == sel;
    if (s) {
      gfx_fillRectMode(x + 8, iy - 4, w - 16, rowH - 2, UI_SEL, GFX_BLEND, 8);
      if (((int)(t * 4.0f)) & 1) gfx_text(x + 14, iy, ">", UI_SEL, 2);
    }
    gfx_textCentered(SCR_CX, iy, items[i], s ? UI_SEL : rgb(210, 210, 220), 2, COL_BLACK);
  }
  gfx_textCentered(SCR_CX, SCR_H - 14, "UP/DOWN SELECT   START CONFIRM", rgb(170, 170, 180), 1, COL_BLACK);
}

void ui_drawMinimap(int x, int y, int size, bool dots) {
  if (size < 8) return;
  const float k = size / 255.0f;
  int px = x + (int)(minimapX[MINIMAP_PTS - 1] * k), py = y + (int)(minimapY[MINIMAP_PTS - 1] * k);
  for (int i = 0; i < MINIMAP_PTS; i++) {
    int nx = x + (int)(minimapX[i] * k), ny = y + (int)(minimapY[i] * k);
    gfx_line(px, py, nx, ny, rgb(230, 230, 236));
    gfx_line(px + 1, py, nx + 1, ny, rgb(120, 120, 130));
    px = nx; py = ny;
  }
  // start line
  int sx = x + (int)(minimapX[0] * k), sy = y + (int)(minimapY[0] * k);
  gfx_fillRect(sx - 2, sy - 1, 5, 3, COL_RED);
  if (!dots) return;
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < NUM_RACERS; i++) {
      bool isPlayer = i == PLAYER_ID;
      if ((pass == 1) != isPlayer) continue;
      float u, v;
      minimapAt(racers[i].z, u, v);
      int dx = x + (int)(u * size), dy = y + (int)(v * size);
      uint16_t c = isPlayer ? COL_WHITE : (racers[i].paint ? racers[i].paint : rgb(60, 90, 220));
      int r = isPlayer ? 2 : 1;
      gfx_fillRect(dx - r, dy - r, 2 * r + 1, 2 * r + 1, isPlayer ? COL_BLACK : c);
      if (isPlayer) gfx_fillRect(dx - 1, dy - 1, 3, 3, c);
    }
  }
}

void ui_drawCircuitSelect(int sel, float t) {
  const int x = 10, y = 18, w = SCR_W - 20, h = 196;
  panel(x, y, w, h);
  gfx_textCentered(SCR_CX, y + 8, "SELECT CIRCUIT", COL_WHITE, 2, rgb(120, 0, 0));
  for (int i = 0; i <= NUM_CIRCUITS; i++) {
    int iy = y + 36 + i * 24;
    bool s = i == sel;
    const char* label = (i < NUM_CIRCUITS) ? CIRCUITS[i].name : "BACK";
    if (s) {
      gfx_fillRectMode(x + 6, iy - 4, 166, 20, UI_SEL, GFX_BLEND, 8);
      if (((int)(t * 4.0f)) & 1) gfx_text(x + 8, iy, ">", UI_SEL, 2);
    }
    gfx_textShadow(x + 22, iy, label, s ? UI_SEL : rgb(210, 210, 220), 2, COL_BLACK);
  }
  if (sel >= 0 && sel < NUM_CIRCUITS) {
    const CircuitInfo& c = CIRCUITS[sel];
    char buf[48], tb[16];
    gfx_textShadow(x + 10, y + 140, c.tagline, rgb(140, 220, 255), 1, COL_BLACK);
    snprintf(buf, sizeof(buf), "%d LAPS", c.laps);
    gfx_textShadow(x + 10, y + 152, buf, rgb(210, 210, 220), 1, COL_BLACK);
    ui_fmtTime(tb, sizeof(tb), saveData.bestLap[sel]);
    snprintf(buf, sizeof(buf), "BEST LAP  %s", saveData.bestLap[sel] > 0 ? tb : "--:--.--");
    gfx_textShadow(x + 10, y + 164, buf, COL_GREEN, 1, COL_BLACK);
    ui_fmtTime(tb, sizeof(tb), saveData.bestRace[sel]);
    snprintf(buf, sizeof(buf), "BEST RACE %s", saveData.bestRace[sel] > 0 ? tb : "--:--.--");
    gfx_textShadow(x + 10, y + 176, buf, COL_GREEN, 1, COL_BLACK);
    // preview of the outline (the select screen builds the circuit)
    if (currentCircuit == sel) {
      gfx_fillRectMode(186, y + 34, 112, 112, COL_BLACK, GFX_BLEND, 12);
      ui_drawMinimap(190, y + 38, 104, false);
    }
  }
}

// ═══════════════════════════════════════════════════════════════
//  RACE OVERLAYS
// ═══════════════════════════════════════════════════════════════
static void startLights(int lit, bool green) {
  const int cx = SCR_CX, cy = 46, gap = 30;
  gfx_fillRectMode(cx - 52, cy - 16, 104, 32, COL_BLACK, GFX_BLEND, 22);
  gfx_rect(cx - 52, cy - 16, 104, 32, rgb(90, 90, 100));
  for (int i = 0; i < 3; i++) {
    int lx = cx + (i - 1) * gap;
    uint16_t c = green ? rgb(40, 255, 80) : (i < lit ? rgb(255, 40, 30) : rgb(70, 16, 16));
    gfx_fillCircle(lx, cy, 10, rgb(20, 20, 24));
    gfx_fillCircle(lx, cy, 8, c);
    if (green || i < lit) gfx_glow(lx, cy, 20, c, 18);
  }
}

void ui_drawCountdown(float t) {
  int n = (int)t;                       // 0: get ready, 1..3: lights
  startLights(imin(n, 3), false);
  if (n <= 0) {
    gfx_textCentered(SCR_CX, 96, "GET READY", COL_WHITE, 3, COL_BLACK);
    gfx_textCentered(SCR_CX, 124, curCircuit().name, UI_SEL, 2, COL_BLACK);
  } else if (n <= 3) {
    char buf[4];
    snprintf(buf, sizeof(buf), "%d", 4 - n);
    float f = t - n;                    // pop in
    int sc = f < 0.15f ? 8 : 6;
    gfx_textCentered(SCR_CX, 84 - (sc - 6) * 3, buf, COL_YELLOW, sc, rgb(120, 60, 0));
  }
}

void ui_drawGo(float since) {
  if (since < 0 || since > 1.2f) return;
  startLights(3, true);
  if (((int)(since * 8.0f)) & 1 && since > 0.6f) return;   // blink out
  gfx_textCentered(SCR_CX, 84, "GO!", rgb(60, 255, 90), 7, rgb(0, 80, 20));
}

void ui_drawPause(int sel) {
  static const char* const ITEMS[2] = { "RESUME", "QUIT RACE" };
  gfx_fillRectMode(0, 0, SCR_W, SCR_H, COL_BLACK, GFX_BLEND, 10);
  ui_drawMenu("PAUSED", ITEMS, 2, sel, 0.0f);
}

static const char* placeSuffix(int p) {
  return p == 1 ? "ST" : p == 2 ? "ND" : p == 3 ? "RD" : "TH";
}

void ui_drawFinish(float t, int place) {
  int y = 70;
  int slide = (int)(fmaxF(0.0f, 0.25f - t) * 4.0f * SCR_W);
  gfx_fillRectMode(0, y - 8, SCR_W, 74, COL_BLACK, GFX_BLEND, 18);
  gfx_fillRect(0, y - 8, SCR_W, 2, COL_WHITE);
  gfx_fillRect(0, y + 64, SCR_W, 2, COL_WHITE);
  gfx_textCentered(SCR_CX + slide, y, "FINISH", COL_WHITE, 4, COL_BLACK);
  char buf[16];
  snprintf(buf, sizeof(buf), "%d%s PLACE", place, placeSuffix(place));
  gfx_textCentered(SCR_CX - slide, y + 38, buf, place == 1 ? UI_SEL : rgb(210, 210, 220), 2, COL_BLACK);
}

void ui_drawResults(const ResultRow* rows, const int* points, float t) {
  const int x = 8, y = 14, w = SCR_W - 16, h = 206;
  panel(x, y, w, h);
  gfx_textCentered(SCR_CX, y + 8, "RESULTS", COL_WHITE, 2, rgb(120, 0, 0));
  gfx_textCentered(SCR_CX, y + 28, curCircuit().name, UI_SEL, 1, COL_BLACK);
  const int hy = y + 44;
  gfx_text(x + 8, hy, "POS NAME     TIME       BEST", rgb(150, 150, 160), 1);
  if (points) gfx_text(x + w - 34, hy, "PTS", rgb(150, 150, 160), 1);
  char buf[64], tb[16], bb[16];
  for (int p = 0; p < NUM_RACERS; p++) {
    const ResultRow& r = rows[p];
    const Racer& R = racers[r.racer];
    int ry = hy + 14 + p * 22;
    bool me = r.racer == PLAYER_ID;
    if (me) gfx_fillRectMode(x + 4, ry - 5, w - 8, 18, UI_SEL, GFX_BLEND, 7);
    ui_fmtTime(tb, sizeof(tb), r.time);
    if (r.bestLap > 0) ui_fmtTime(bb, sizeof(bb), r.bestLap);
    else snprintf(bb, sizeof(bb), "--");
    snprintf(buf, sizeof(buf), "%d   %-8s %s%s", p + 1, R.name, tb, r.finished ? " " : "*");
    gfx_textShadow(x + 8, ry, buf, me ? UI_SEL : COL_WHITE, 1, COL_BLACK);
    gfx_textShadow(x + 170, ry, bb, rgb(150, 230, 150), 1, COL_BLACK);
    gfx_fillRect(x + 150, ry, 8, 7, R.paint ? R.paint : rgb(40, 52, 110));
    if (points) {
      snprintf(buf, sizeof(buf), "+%d", points[r.racer]);
      gfx_textShadow(x + w - 34, ry, buf, UI_SEL, 1, COL_BLACK);
    }
  }
  gfx_text(x + 8, y + h - 24, "* ESTIMATED", rgb(130, 130, 140), 1);
  if (t > 1.0f && (((int)(t * 2.0f)) & 1))
    gfx_textCentered(SCR_CX, y + h - 13, "PRESS START", COL_WHITE, 1, COL_BLACK);
}

void ui_drawStandings(const int* order, const int* points, int raceNo, int races, float t) {
  const int x = 30, y = 20, w = SCR_W - 60, h = 196;
  panel(x, y, w, h);
  char buf[48];
  gfx_textCentered(SCR_CX, y + 8, "GRAND PRIX", COL_WHITE, 2, rgb(120, 0, 0));
  snprintf(buf, sizeof(buf), "STANDINGS AFTER RACE %d/%d", raceNo, races);
  gfx_textCentered(SCR_CX, y + 28, buf, UI_SEL, 1, COL_BLACK);
  for (int p = 0; p < NUM_RACERS; p++) {
    int i = order[p];
    int ry = y + 48 + p * 22;
    bool me = i == PLAYER_ID;
    if (me) gfx_fillRectMode(x + 4, ry - 5, w - 8, 18, UI_SEL, GFX_BLEND, 7);
    gfx_fillRect(x + 34, ry, 8, 7, racers[i].paint ? racers[i].paint : rgb(40, 52, 110));
    snprintf(buf, sizeof(buf), "%d", p + 1);
    gfx_textShadow(x + 12, ry, buf, me ? UI_SEL : COL_WHITE, 1, COL_BLACK);
    gfx_textShadow(x + 50, ry, racers[i].name, me ? UI_SEL : COL_WHITE, 1, COL_BLACK);
    snprintf(buf, sizeof(buf), "%3d PTS", points[i]);
    gfx_textShadow(x + w - 60, ry, buf, me ? UI_SEL : COL_WHITE, 1, COL_BLACK);
  }
  if (t > 1.0f && (((int)(t * 2.0f)) & 1))
    gfx_textCentered(SCR_CX, y + h - 13, "PRESS START", COL_WHITE, 1, COL_BLACK);
}

void ui_drawChampion(int winner, int points, float t) {
  panel(20, 40, SCR_W - 40, 150);
  gfx_textCentered(SCR_CX, 52, "CHAMPION", UI_SEL, 4, rgb(120, 60, 0));
  gfx_textCentered(SCR_CX, 96, racers[winner].name, COL_WHITE, 3, COL_BLACK);
  char buf[32];
  snprintf(buf, sizeof(buf), "%d POINTS", points);
  gfx_textCentered(SCR_CX, 128, buf, rgb(210, 210, 220), 2, COL_BLACK);
  if (winner == PLAYER_ID) gfx_textCentered(SCR_CX, 152, "YOU WIN THE GRAND PRIX!", COL_GREEN, 1, COL_BLACK);
  if (t > 1.0f && (((int)(t * 2.0f)) & 1))
    gfx_textCentered(SCR_CX, 174, "PRESS START", COL_WHITE, 1, COL_BLACK);
}

void ui_drawDemoBanner(float t) {
  if (((int)(t * 1.5f)) & 1) gfx_textCentered(SCR_CX, 64, "DEMO", COL_WHITE, 3, COL_BLACK);
  gfx_textCentered(SCR_CX, SCR_H - 28, "PRESS START", COL_YELLOW, 1, COL_BLACK);
}

void ui_drawCrash(float since) {
  if (since < 0.3f) {   // impact flash
    int a = (int)((1.0f - since / 0.3f) * 18.0f);
    gfx_fillRectMode(0, 0, SCR_W, SCR_H, COL_WHITE, GFX_BLEND, a);
  }
  gfx_fillRectMode(0, SCR_CY - 22, SCR_W, 44, COL_BLACK, GFX_BLEND, 18);
  gfx_fillRect(0, SCR_CY - 22, SCR_W, 2, COL_RED);
  gfx_fillRect(0, SCR_CY + 20, SCR_W, 2, COL_RED);
  gfx_textCentered(SCR_CX, SCR_CY - 10, "CRASH!", COL_RED, 3, COL_BLACK);
}

void ui_drawBanner(const char* text, uint16_t color, float t) {
  if (t < 0 || t > 2.2f) return;
  int slide = (int)(fmaxF(0.0f, 0.2f - t) * 5.0f * SCR_W);
  gfx_fillRectMode(0, 62, SCR_W, 30, COL_BLACK, GFX_BLEND, 16);
  gfx_textCentered(SCR_CX + slide, 70, text, color, 2, COL_BLACK);
}

void ui_fade(float t, float dur) {
  if (!(dur > 0) || t >= dur) return;
  int a = (int)((1.0f - clampF(t / dur, 0, 1)) * 32.0f);
  if (a > 0) gfx_fillRectMode(0, 0, SCR_W, SCR_H, COL_BLACK, GFX_BLEND, a);
}
