/*
  ═══════════════════════════════════════════════════════════════
  IMPLEMENTACIÓN DE RENDERIZADO
  ═══════════════════════════════════════════════════════════════
*/

#include "rendering.h"
#include "config.h"
#include "colors.h"
#include "utils.h"
#include "track.h"

// ═══════════════════════════════════════════════════════════════
//  VARIABLES GLOBALES (Definición)
// ═══════════════════════════════════════════════════════════════
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);

RenderPt rCache[DRAW_DIST];
int16_t  rClip[DRAW_DIST];

// ═══════════════════════════════════════════════════════════════
//  IMPLEMENTACIÓN DE FUNCIONES
// ═══════════════════════════════════════════════════════════════

void drawSpriteShape(int type, int sx, int sy, float scale, int16_t clipY, int timeOfDay) {
  int bottomY = min((int)sy, (int)clipY);
  if (bottomY <= 0 || sx < -60 || sx > SCR_W + 60) return;

  switch (type) {
    case 0: { // Pino
      int h = (int)(scale * 28000);
      int w = (int)(scale * 10000);
      if (h < 3 || w < 2) return;
      int trunkH = h / 4, trunkW = max(2, w / 5);
      uint16_t gc = (timeOfDay == 2) ? rgb(0, 50, 10) : rgb(0, 130, 25);
      spr.fillRect(sx - trunkW / 2, bottomY - trunkH, trunkW, trunkH, rgb(80, 50, 20));
      spr.fillTriangle(sx, bottomY - h, sx - w / 2, bottomY - trunkH, sx + w / 2, bottomY - trunkH, gc);
      break;
    }
    case 1: { // Árbol redondeado
      int h = (int)(scale * 26000);
      int w = (int)(scale * 12000);
      if (h < 3 || w < 2) return;
      int trunkH = h * 2 / 3, trunkW = max(2, w / 8);
      int leafR  = max(2, w / 2);
      uint16_t lc = (timeOfDay == 2) ? rgb(0, 55, 12) : rgb(0, 140, 30);
      spr.fillRect(sx - trunkW / 2, bottomY - trunkH, trunkW, trunkH, rgb(120, 80, 30));
      spr.fillCircle(sx, bottomY - trunkH - leafR / 2, leafR, lc);
      break;
    }
    case 2: { // Arbusto
      int r = max(2, (int)(scale * 6000));
      uint16_t bc = (timeOfDay == 2) ? rgb(0, 48, 10) : rgb(20, 130, 20);
      spr.fillCircle(sx, bottomY - r, r, bc);
      break;
    }
    case 3: { // Roca
      int rh = max(2, (int)(scale * 6000));
      int rw = max(3, (int)(scale * 8000));
      spr.fillRect(sx - rw / 2, bottomY - rh, rw, rh, rgb(100, 95, 90));
      break;
    }
    case 4: { // Poste
      int ph = max(3, (int)(scale * 14000));
      int pw = max(2, 3);
      spr.fillRect(sx - pw / 2, bottomY - ph, pw, ph, TFT_WHITE);
      if (ph > 6)
        spr.fillRect(sx - pw / 2, bottomY - ph, pw, 3, TFT_RED);
      break;
    }
  }
}

void drawTrafficCar(int cx, int cy, float scale, uint16_t col, int16_t clipY) {
  int w = (int)(scale * 30000);
  int h = (int)(scale * 18000);
  if (w < 3 || h < 2) return;
  int bottomY = min(cy, (int)clipY);
  int topY = bottomY - h;
  if (topY >= SCR_H || bottomY < 0) return;

  spr.fillRect(cx - w / 2, topY, w, h * 2 / 3, col);
  int rw = w * 2 / 3;
  spr.fillRect(cx - rw / 2, topY - h / 4, rw, h / 3, darkenCol(col, 0.8));
  if (h > 6) {
    int ww = rw - 4;
    if (ww > 2)
      spr.fillRect(cx - ww / 2, topY - h / 4 + 1, ww, max(1, h / 5), rgb(150, 200, 255));
  }
  if (w > 8) {
    spr.fillRect(cx - w / 2 + 1, topY + 1, 2, max(1, h / 6), TFT_RED);
    spr.fillRect(cx + w / 2 - 3, topY + 1, 2, max(1, h / 6), TFT_RED);
  }
}

void drawPlayerCar() {
  const int cW = 44, cH = 28;
  int cx = SCR_CX, cy = SCR_H - 20;

  spr.fillRect(cx - cW / 2 + 3, cy + 2, cW - 6, 4, rgb(30, 30, 30));
  spr.fillRect(cx - cW / 2 - 3, cy - 12, 5, 14, rgb(20, 20, 20));
  spr.fillRect(cx + cW / 2 - 2, cy - 12, 5, 14, rgb(20, 20, 20));
  spr.fillRect(cx - cW / 2, cy - 18, cW, 18, rgb(220, 20, 20));
  int rw = cW - 16;
  spr.fillRect(cx - rw / 2, cy - cH, rw, 14, rgb(200, 15, 15));
  spr.fillRect(cx - rw / 2 + 3, cy - cH + 2, rw - 6, 6, rgb(100, 180, 255));
  spr.fillRect(cx - cW / 2 + 2, cy - 14, 6, 4, TFT_ORANGE);
  spr.fillRect(cx + cW / 2 - 8, cy - 14, 6, 4, TFT_ORANGE);
  spr.drawFastHLine(cx - 1, cy - 18, 3, TFT_WHITE);
  spr.drawFastHLine(cx - 1, cy - 10, 3, TFT_WHITE);
}

void drawSky(float position, float playerZdist, int timeOfDay) {
  int pSegIdx = findSegIdx(position + playerZdist);
  if (segments[pSegIdx].tunnel) {
    uint16_t roofCol = rgb(25, 22, 20);
    spr.fillRect(0, 0, SCR_W, SCR_CY, roofCol);
    // Luces del túnel
    for (int i = 0; i < 5; i++) {
      int lx = 40 + i * 60;
      spr.fillCircle(lx, SCR_CY - 10, 3, rgb(200, 180, 50));
      spr.fillCircle(lx, SCR_CY - 10, 1, rgb(255, 255, 150));
    }
    return;
  }

  int h = SCR_CY, band = h / 3;
  spr.fillRect(0, 0,      SCR_W, band,         colSky1);
  spr.fillRect(0, band,   SCR_W, band,         colSky2);
  spr.fillRect(0, band*2, SCR_W, h - band * 2, colSky3);

  if (timeOfDay == 2) {
    static const uint16_t stX[] = {15,45,78,120,155,190,225,260,290,310,33,67,105,145,185,230,275};
    static const uint8_t stY[] = {8,25,15,5,30,12,22,8,18,28,40,48,35,50,42,55,38};
    for (int i = 0; i < 17; i++) spr.drawPixel(stX[i], stY[i], TFT_WHITE);
  }
  spr.drawFastHLine(0, h, SCR_W, lerpCol(colSky3, TFT_WHITE, 0.3));
}

void drawQuad(int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4, uint16_t c) {
  spr.fillTriangle(x1, y1, x2, y2, x3, y3, c);
  spr.fillTriangle(x1, y1, x3, y3, x4, y4, c);
}

void drawRoad(float position, float playerX, float playerZdist,
              float cameraDepth, int timeOfDay) {
  int baseIdx = findSegIdx(position);
  float basePct = percentRemaining(position, SEG_LEN);
  float posOff  = fmodf(position, (float)SEG_LEN);
  if (posOff < 0) posOff += SEG_LEN;

  int pSegIdx  = findSegIdx(position + playerZdist);
  int pPrevIdx = (pSegIdx - 1 + TOTAL_SEGS) % TOTAL_SEGS;
  float pPct   = percentRemaining(position + playerZdist, SEG_LEN);
  float playerY = lerpF(segments[pPrevIdx].y, segments[pSegIdx].y, pPct);
  float camY    = playerY + CAM_HEIGHT;

  float curveX  = 0;
  float curveDX = -(segments[baseIdx].curve * basePct);
  int maxy = SCR_H;

  for (int n = 0; n < DRAW_DIST; n++) {
    int sIdx = (baseIdx + n) % TOTAL_SEGS;
    int prev = (sIdx - 1 + TOTAL_SEGS) % TOTAL_SEGS;
    Segment& seg = segments[sIdx];

    float camZ1 = (float)n       * SEG_LEN - posOff;
    float camZ2 = (float)(n + 1) * SEG_LEN - posOff;

    rCache[n] = {(int16_t)SCR_CX, (int16_t)SCR_H, 0, 0.0f};
    rClip[n]  = maxy;

    if (camZ1 <= cameraDepth) continue;

    float p1Y = segments[prev].y;
    float p2Y = seg.y;
    if (n == 0) {
      int pp = (prev - 1 + TOTAL_SEGS) % TOTAL_SEGS;
      p1Y = lerpF(segments[pp].y, segments[prev].y, basePct);
    }

    float sc1   = cameraDepth / camZ1;
    float cyp1  = p1Y - camY;
    float cxp1  = playerX * ROAD_W - curveX;
    int16_t sy1 = SCR_CY - (int)(sc1 * cyp1 * SCR_CY);
    int16_t sx1 = SCR_CX + (int)(sc1 * (-cxp1) * SCR_CX);
    int16_t sw1 = (int)(sc1 * ROAD_W * SCR_CX);

    float sc2   = (camZ2 > cameraDepth) ? cameraDepth / camZ2 : sc1;
    float cyp2  = p2Y - camY;
    float cxp2  = playerX * ROAD_W - curveX - curveDX;
    int16_t sy2 = SCR_CY - (int)(sc2 * cyp2 * SCR_CY);
    int16_t sx2 = SCR_CX + (int)(sc2 * (-cxp2) * SCR_CX);
    int16_t sw2 = (int)(sc2 * ROAD_W * SCR_CX);

    curveX  += curveDX;
    curveDX += seg.curve;

    rCache[n] = {sx1, sy1, sw1, sc1};

    if (sy1 <= sy2)  { rClip[n] = maxy; continue; }
    if (sy2 >= maxy) { rClip[n] = maxy; continue; }

    rClip[n] = maxy;

    int drawTop = max((int)sy2, 0);
    int drawBot = min((int)sy1, maxy);
    int bandH   = drawBot - drawTop;
    if (bandH <= 0) continue;

    float fogF = expFog((float)n / DRAW_DIST, FOG_DENSITY);
    bool isLight  = ((sIdx / RUMBLE_LEN) % 2) == 0;
    uint16_t grassCol = lerpCol(isLight ? colGrassL : colGrassD, colFog, fogF);
    uint16_t wallCol  = isLight ? rgb(35, 32, 30) : rgb(28, 25, 22);
    uint16_t grass    = seg.tunnel ? wallCol : grassCol;
    uint16_t road   = lerpCol(isLight ? colRoadL   : colRoadD,   colFog, seg.tunnel ? 0 : fogF);
    uint16_t rumble = lerpCol(isLight ? colRumbleL : colRumbleD, colFog, seg.tunnel ? 0 : fogF);
    uint16_t lane   = lerpCol(colLane, colFog, seg.tunnel ? 0 : fogF);

    int subDiv = (bandH > 6) ? min(3, bandH / 3) : 1;
    for (int s = 0; s < subDiv; s++) {
      int subTop = drawTop + s * bandH / subDiv;
      int subBot = drawTop + (s + 1) * bandH / subDiv;
      int subH   = subBot - subTop;
      if (subH <= 0) continue;

      float tMid = (float)(2 * s + 1) / (float)(2 * subDiv);
      int cx  = (int)lerpF(sx2, sx1, tMid);
      int hw  = (int)lerpF(sw2, sw1, tMid);
      int rw  = max(1, hw / 6);
      int rdL = cx - hw,  rdR = cx + hw;
      int rmL = rdL - rw, rmR = rdR + rw;

      int e1 = max(0, min(rmL, SCR_W));
      if (e1 > 0) spr.fillRect(0, subTop, e1, subH, grass);

      int a2 = max(0, rmL), b2 = max(0, min(rdL, SCR_W));
      if (b2 > a2) spr.fillRect(a2, subTop, b2 - a2, subH, rumble);

      int a3 = max(0, rdL), b3 = min(SCR_W, rdR);
      if (b3 > a3) spr.fillRect(a3, subTop, b3 - a3, subH, road);

      int a4 = max(0, rdR), b4 = min(SCR_W, rmR);
      if (b4 > a4) spr.fillRect(a4, subTop, b4 - a4, subH, rumble);

      int s5 = max(0, min(rmR, SCR_W));
      if (s5 < SCR_W) spr.fillRect(s5, subTop, SCR_W - s5, subH, grass);
    }

    if (isLight && sw1 > 15 && bandH > 1) {
      int midX = (sx1 + sx2) / 2;
      int midW = (sw1 + sw2) / 2;
      int midY = drawTop + bandH / 2;
      int lh   = min(bandH, 3);
      for (int l = 1; l < LANES; l++) {
        float lt = (float)l / LANES;
        int lx = (int)lerpF(midX - midW, midX + midW, lt);
        int lw = max(1, midW / 30);
        spr.fillRect(lx - lw / 2, midY, lw, lh, lane);
      }
    }
    maxy = drawTop;
  }

  if (maxy > SCR_CY) {
    spr.fillRect(0, SCR_CY, SCR_W, maxy - SCR_CY, lerpCol(colGrassD, colFog, 0.7));
  }

  // --- RENDERIZADO 3D POLIGONAL (DE ATRÁS HACIA ADELANTE) ---
  for (int n = DRAW_DIST - 1; n > 1; n--) {
    int sIdx = (baseIdx + n) % TOTAL_SEGS;
    int prevIdx = (sIdx - 1 + TOTAL_SEGS) % TOTAL_SEGS;
    Segment& seg = segments[sIdx];
    Segment& prevSeg = segments[prevIdx];

    RenderPt& p1 = rCache[n];
    RenderPt& p0 = rCache[n - 1];

    if (p0.scale <= 0 || p1.scale <= 0 || p0.y >= SCR_H) continue;

    // TÚNEL EN 3D
    if (seg.tunnel) {
      int tw1 = p1.w * 1.8;
      int tw0 = p0.w * 1.8;
      int th1 = (int)(p1.scale * 80000);
      int th0 = (int)(p0.scale * 80000);

      uint16_t wCol = (n % 2 == 0) ? rgb(80, 70, 60) : rgb(70, 60, 50);
      uint16_t rCol = (n % 2 == 0) ? rgb(60, 50, 40) : rgb(50, 40, 30);

      drawQuad(p0.x - tw0, p0.y, p1.x - tw1, p1.y,
               p1.x - tw1, p1.y - th1, p0.x - tw0, p0.y - th0, wCol);
      drawQuad(p0.x + tw0, p0.y, p1.x + tw1, p1.y,
               p1.x + tw1, p1.y - th1, p0.x + tw0, p0.y - th0, wCol);
      drawQuad(p0.x - tw0, p0.y - th0, p1.x - tw1, p1.y - th1,
               p1.x + tw1, p1.y - th1, p0.x + tw0, p0.y - th0, rCol);

      if (!prevSeg.tunnel) {
        int wOut = p0.w * 4;
        int hOut = (int)(p0.scale * 120000);
        uint16_t rockCol = rgb(110, 90, 70);
        spr.fillRect(p0.x - wOut, p0.y - hOut, wOut - tw0, hOut, rockCol);
        spr.fillRect(p0.x + tw0,  p0.y - hOut, wOut - tw0, hOut, rockCol);
        spr.fillRect(p0.x - tw0,  p0.y - hOut, tw0 * 2, hOut - th0, rockCol);
      }
    }

    // EDIFICIOS 3D
    if (!seg.tunnel) {
      if (seg.buildL > 0) {
        int h1 = (int)(p1.scale * seg.buildL);
        int h0 = (int)(p0.scale * seg.buildL);
        int off1 = p1.w * 1.5; int off0 = p0.w * 1.5;
        int bw1 = (int)(p1.scale * 50000);
        int bw0 = (int)(p0.scale * 50000);

        drawQuad(p0.x - off0, p0.y, p1.x - off1, p1.y,
                 p1.x - off1, p1.y - h1, p0.x - off0, p0.y - h0,
                 darkenCol(seg.colorL, 0.6));
        drawQuad(p0.x - off0, p0.y - h0, p1.x - off1, p1.y - h1,
                 p1.x - off1 - bw1, p1.y - h1, p0.x - off0 - bw0, p0.y - h0,
                 darkenCol(seg.colorL, 0.85));

        if (prevSeg.buildL == 0) {
          drawQuad(p0.x - off0, p0.y, p0.x - off0 - bw0, p0.y,
                   p0.x - off0 - bw0, p0.y - h0, p0.x - off0, p0.y - h0,
                   seg.colorL);
          if (h0 > 12 && bw0 > 6)
            spr.fillRect(p0.x - off0 - bw0 * 3 / 4, p0.y - h0 * 5 / 6,
                         bw0 / 2, h0 / 4, rgb(255, 255, 180));
        }
      }

      if (seg.buildR > 0) {
        int h1 = (int)(p1.scale * seg.buildR);
        int h0 = (int)(p0.scale * seg.buildR);
        int off1 = p1.w * 1.5; int off0 = p0.w * 1.5;
        int bw1 = (int)(p1.scale * 50000);
        int bw0 = (int)(p0.scale * 50000);

        drawQuad(p0.x + off0, p0.y, p1.x + off1, p1.y,
                 p1.x + off1, p1.y - h1, p0.x + off0, p0.y - h0,
                 darkenCol(seg.colorR, 0.6));
        drawQuad(p0.x + off0, p0.y - h0, p1.x + off1, p1.y - h1,
                 p1.x + off1 + bw1, p1.y - h1, p0.x + off0 + bw0, p0.y - h0,
                 darkenCol(seg.colorR, 0.85));

        if (prevSeg.buildR == 0) {
          drawQuad(p0.x + off0, p0.y, p0.x + off0 + bw0, p0.y,
                   p0.x + off0 + bw0, p0.y - h0, p0.x + off0, p0.y - h0,
                   seg.colorR);
          if (h0 > 12 && bw0 > 6)
            spr.fillRect(p0.x + off0 + bw0 / 4, p0.y - h0 * 5 / 6,
                         bw0 / 2, h0 / 4, rgb(255, 255, 180));
        }
      }
    }

    // Sprites normales
    if (seg.spriteType >= 0) {
      int sprX = p1.x + (int)(p1.scale * seg.spriteOffset * ROAD_W * SCR_CX);
      drawSpriteShape(seg.spriteType, sprX, p1.y, p1.scale, rClip[n], timeOfDay);
    }

    // Tráfico
    for (int c = 0; c < MAX_CARS; c++) {
      if (findSegIdx(trafficCars[c].z) != sIdx) continue;
      int carX = p1.x + (int)(p1.scale * trafficCars[c].offset * ROAD_W * SCR_CX);
      drawTrafficCar(carX, p1.y, p1.scale, trafficCars[c].color, rClip[n]);
    }
  }
}

void drawHUD(float speed, float maxSpeed, float currentLapTime, float bestLapTime) {
  int kmh = (int)(speed * 300.0 / maxSpeed);

  spr.fillRect(0, 0, 130, 18, 0x0000);
  spr.setTextSize(1);
  spr.setTextColor(TFT_YELLOW, TFT_BLACK);
  spr.setCursor(2, 2);
  spr.print("SPD ");
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.print(kmh);
  spr.print(" km/h ");

  spr.setCursor(2, 11);
  spr.setTextColor(TFT_CYAN, TFT_BLACK);
  spr.print("LAP ");
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.print((int)currentLapTime);
  spr.print(".");
  spr.print((int)((currentLapTime - (int)currentLapTime) * 10));
  spr.print("s ");

  if (bestLapTime > 0 && bestLapTime < 999) {
    spr.setCursor(SCR_W - 82, 2);
    spr.setTextColor(TFT_GREEN, TFT_BLACK);
    spr.print("BEST ");
    spr.print((int)bestLapTime);
    spr.print(".");
    spr.print((int)((bestLapTime - (int)bestLapTime) * 10));
    spr.print(" ");
  }

  int barX = SCR_W - 105, barW = 100, barH = 5, barY = 13;
  int fill = (int)(speed / maxSpeed * barW);
  uint16_t bc = TFT_GREEN;
  if (speed > maxSpeed * 0.7) bc = TFT_YELLOW;
  if (speed > maxSpeed * 0.9) bc = TFT_RED;
  spr.drawRect(barX - 1, barY - 1, barW + 2, barH + 2, rgb(80, 80, 80));
  if (fill > 0)    spr.fillRect(barX, barY, fill, barH, bc);
  if (fill < barW) spr.fillRect(barX + fill, barY, barW - fill, barH, 0);
}

void drawStartScreen() {
  spr.fillSprite(TFT_BLACK);
  spr.fillRect(0, 30, SCR_W, 3, TFT_RED);
  spr.fillRect(0, 35, SCR_W, 3, TFT_WHITE);
  spr.fillRect(0, 115, SCR_W, 3, TFT_WHITE);
  spr.fillRect(0, 120, SCR_W, 3, TFT_RED);

  spr.setTextColor(TFT_RED);
  spr.setTextSize(3);
  spr.setCursor(22, 50);
  spr.print("OUTRUN ESP32");

  spr.setTextSize(2);
  spr.setTextColor(TFT_YELLOW);
  spr.setCursor(60, 85);
  spr.print("3D RACING");

  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE);
  spr.setCursor(30, 140);
  spr.print("LEFT / RIGHT buttons to steer");
  spr.setCursor(30, 155);
  spr.print("Car accelerates automatically");
  spr.setCursor(30, 170);
  spr.print("Stay on road! Avoid traffic!");

  spr.setTextColor(TFT_CYAN);
  spr.setCursor(25, 195);
  spr.print("Hills + Curves + Fog + Traffic");

  spr.setTextColor(rgb(120, 120, 120));
  spr.setCursor(85, 220);
  spr.print("Get ready...");

  spr.pushSprite(0, 0);
}

void drawCrashMessage() {
  spr.fillRect(SCR_CX - 70, SCR_CY - 15, 140, 30, TFT_BLACK);
  spr.drawRect(SCR_CX - 71, SCR_CY - 16, 142, 32, TFT_RED);
  spr.setTextSize(3);
  spr.setTextColor(TFT_RED, TFT_BLACK);
  spr.setCursor(SCR_CX - 55, SCR_CY - 8);
  spr.print("CRASH!");
}
