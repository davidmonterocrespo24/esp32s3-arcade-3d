/*
  ═══════════════════════════════════════════════════════════════
  IMPLEMENTACIÓN DE RENDERIZADO DE CARRETERA, TÚNELES Y EDIFICIOS
  ═══════════════════════════════════════════════════════════════
*/

#include "render_road.h"
#include "rendering.h"
#include "render_traffic.h"
#include "config.h"
#include "colors.h"
#include "utils.h"
#include "track.h"
#include "physics.h"
#include "render_building.h"

// Variables externas necesarias
extern RenderPt rCache[DRAW_DIST];
extern int16_t  rClip[DRAW_DIST];
extern TFT_eSprite bgSpr;
extern bool bgCreated;

void drawQuad(int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4, uint16_t c) {
  spr.fillTriangle(x1, y1, x2, y2, x3, y3, c);
  spr.fillTriangle(x1, y1, x3, y3, x4, y4, c);
}

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

void drawSky(float position, float playerZdist, int timeOfDay, float skyOffset) {
  int pSegIdx = findSegIdx(position + playerZdist);

  // --- EFECTO PARALLAX INFINITO (Estilo Horizon Chase) ---

  // Fallback si la PSRAM falló
  if (!bgCreated) {
    // Dibujar cielo plano simple para evitar fondo negro/basura
    uint16_t fallbackTop = rgb(40, 40, 80);
    uint16_t fallbackBot = rgb(150, 100, 150);
    // Degradado manual simple (rectangulos)
    spr.fillRect(0, 0, SCR_W, SCR_CY/2, fallbackTop);
    spr.fillRect(0, SCR_CY/2, SCR_W, SCR_CY/2, fallbackBot);
  } else {
    // Calculamos desplazamiento X con wrap-around sobre buffer doble (640px)
    int bgX = (int)skyOffset % (SCR_W * 2);
    while (bgX < 0) bgX += (SCR_W * 2); // Asegurar positivo

    // Dibujamos el fondo DOS VECES para seamless scrolling
    // El buffer tiene 640px de ancho, así que el sol (en x=320) solo se ve una vez en pantalla (320px)
    bgSpr.pushToSprite(&spr, -bgX, 0);
    bgSpr.pushToSprite(&spr, (SCR_W * 2) - bgX, 0);
  }

  // Estrellas por la noche (opcional, sobre el parallax)
  if (timeOfDay == 2) {
    static const uint16_t PROGMEM stX[] = {15,45,78,120,155,190,225,260,290,310,33,67,105,145,185,230,275};
    static const uint8_t PROGMEM stY[] = {8,25,15,5,30,12,22,8,18,28,40,48,35,50,42,55,38};
    for (int i = 0; i < 17; i++) {
      spr.drawPixel(pgm_read_word(&stX[i]), pgm_read_byte(&stY[i]), TFT_WHITE);
    }
  }

  // Línea de horizonte
  spr.drawFastHLine(0, SCR_CY, SCR_W, rgb(100, 100, 100));
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
  int maxy = SCR_H; // Horizonte del suelo (sube)
  int minCeilY = 0; // Horizonte del techo (baja)

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

    // TECHO DEL TÚNEL: Se dibuja en el loop 3D (back-to-front) junto con las paredes

    // Guardamos info para dibujar después (no dibujamos la carretera aquí)
    maxy = drawTop;
  }

  if (maxy > SCR_CY) {
    // Relleno de suelo (solo si no es túnel, o si, para evitar huecos)
     spr.fillRect(0, SCR_CY, SCR_W, maxy - SCR_CY, lerpCol(colGrassD, colFog, 0.7));
  }

  // --- PRIMER PASO: RENDERIZADO 3D DE EDIFICIOS Y TÚNELES (DE ATRÁS HACIA ADELANTE) ---
  for (int n = DRAW_DIST - 1; n > 1; n--) {
    int sIdx = (baseIdx + n) % TOTAL_SEGS;
    int prevIdx = (sIdx - 1 + TOTAL_SEGS) % TOTAL_SEGS;
    Segment& seg = segments[sIdx];
    Segment& prevSeg = segments[prevIdx];

    RenderPt& p1 = rCache[n];
    RenderPt& p0 = rCache[n - 1];

    if (p0.scale <= 0 || p1.scale <= 0 || p0.y >= SCR_H) continue;

    // TÚNEL EN 3D (BACK-TO-FRONT) - Techo primero, luego paredes
    if (seg.tunnel) {
       // Determinar colores con alternancia
       bool isLightT = ((sIdx / 3) % 2) == 0;
       uint16_t wallT = isLightT ? rgb(80, 120, 200) : rgb(50, 80, 150);  // Azul

       // Calcular colores del techo (mismo patrón que la carretera)
       bool isLightCeil = ((sIdx / RUMBLE_LEN) % 2) == 0;
       uint16_t roadCeilL = isLightCeil ? colRoadL : colRoadD;
       uint16_t roadCeilD = isLightCeil ? colRoadD : colRoadL;
       uint16_t ceilColor = isLightCeil ? roadCeilL : roadCeilD;

       // Altura del techo
       float cH = 4500.0f;
       int cy1 = SCR_CY - (int)(p1.scale * (seg.y + cH - camY) * SCR_CY);
       int cy0 = SCR_CY - (int)(p0.scale * (prevSeg.y + cH - camY) * SCR_CY);

       // Bordes de la carretera (suelo)
       int roadL0 = p0.x - p0.w;
       int roadR0 = p0.x + p0.w;
       int roadL1 = p1.x - p1.w;
       int roadR1 = p1.x + p1.w;

       // Bordes del techo: mismo ancho que la carretera para paredes 90°
       int ceilL0 = roadL0;
       int ceilR0 = roadR0;
       int ceilL1 = roadL1;
       int ceilR1 = roadR1;

       // 0. FONDO DEL TÚNEL (negro) — solo en el último segmento tunnel antes de salida
       {
         int nextIdx = (sIdx + 1) % TOTAL_SEGS;
         if (!segments[nextIdx].tunnel) {
           int intTop = cy1;
           int intBot = (int)p1.y;
           int intL   = max(roadL1, 0);
           int intR   = min(roadR1, SCR_W);
           if (intBot > intTop && intR > intL)
             spr.fillRect(intL, intTop, intR - intL, intBot - intTop, TFT_BLACK);
         }
       }

       // 1. TECHO (espejo de la carretera)
       // Vértices: izquierda-cerca, derecha-cerca, derecha-lejos, izquierda-lejos
       drawQuad(ceilL0, cy0, ceilR0, cy0, ceilR1, cy1, ceilL1, cy1, ceilColor);

       // 2. Pared Izquierda VERTICAL
       drawQuad(roadL0, p0.y, roadL1, p1.y, ceilL1, cy1, ceilL0, cy0, wallT);

       // 3. Pared Derecha VERTICAL
       drawQuad(roadR0, p0.y, ceilR0, cy0, ceilR1, cy1, roadR1, p1.y, wallT);
    }

    // EDIFICIOS 3D CON VENTANAS (Estilo Horizon Chase/Nueva York)
    if (!seg.tunnel) {
      // --- EDIFICIO IZQUIERDO ---
      if (seg.buildL > 0) {
         bool showFront = (prevSeg.buildL == 0 || prevSeg.buildL != seg.buildL) && !prevSeg.tunnel;
         drawBuilding(p0, p1, seg.buildL, seg.colorL, sIdx, true, showFront);
      }

      // --- EDIFICIO DERECHO ---
      if (seg.buildR > 0) {
         bool showFront = (prevSeg.buildR == 0 || prevSeg.buildR != seg.buildR) && !prevSeg.tunnel;
         drawBuilding(p0, p1, seg.buildR, seg.colorR, sIdx, false, showFront);
      }
    }

  }

  // --- SEGUNDO PASO: DIBUJAR LA CARRETERA ENCIMA (DE ATRÁS HACIA ADELANTE) ---
  maxy = SCR_H;
  for (int n = 0; n < DRAW_DIST; n++) {
    int sIdx = (baseIdx + n) % TOTAL_SEGS;
    Segment& seg = segments[sIdx];

    RenderPt& p1 = rCache[n];
    if (n == 0 || p1.scale <= 0) continue;

    RenderPt& p0 = rCache[n - 1];
    if (p0.scale <= 0) continue;

    int drawTop = max((int)p1.y, 0);
    int drawBot = min((int)p0.y, maxy);
    int bandH = drawBot - drawTop;
    if (bandH <= 0) continue;

    float fogF = expFog((float)n / DRAW_DIST, FOG_DENSITY);
    bool isLight = ((sIdx / RUMBLE_LEN) % 2) == 0;
    uint16_t grassCol = lerpCol(isLight ? colGrassL : colGrassD, colFog, fogF);
    uint16_t wallCol = isLight ? rgb(35, 32, 30) : rgb(28, 25, 22);
    uint16_t grass = seg.tunnel ? wallCol : grassCol;
    uint16_t road = lerpCol(isLight ? colRoadL : colRoadD, colFog, seg.tunnel ? 0 : fogF);
    uint16_t rumble = lerpCol(isLight ? colRumbleL : colRumbleD, colFog, seg.tunnel ? 0 : fogF);
    uint16_t lane = lerpCol(colLane, colFog, seg.tunnel ? 0 : fogF);

    int subDiv = (bandH > 6) ? min(3, bandH / 3) : 1;
    for (int s = 0; s < subDiv; s++) {
      int subTop = drawTop + s * bandH / subDiv;
      int subBot = drawTop + (s + 1) * bandH / subDiv;
      int subH = subBot - subTop;
      if (subH <= 0) continue;

      float tMid = (float)(2 * s + 1) / (float)(2 * subDiv);
      int cx = (int)lerpF(p1.x, p0.x, tMid);
      int hw = (int)lerpF(p1.w, p0.w, tMid);
      int rdL = cx - hw, rdR = cx + hw;

      // Césped / Paredes Planas (Base)
      int rw = max(1, hw / 6);
      int rmL = rdL - rw, rmR = rdR + rw;

      if (seg.tunnel) {
        // PAREDES LATERALES DEL TÚNEL — fillRect por scanline = siempre 90° con la carretera
        bool iLightT = ((sIdx / 3) % 2) == 0;
        uint16_t wCol = iLightT ? rgb(80, 120, 200) : rgb(50, 80, 150);

        // Ancho de pared proporcional al segmento (perspectiva correcta)
        int wallW = max(2, hw / 5);

        // Pared izquierda: rellena desde borde izq hasta el límite de la pantalla
        int wlR = max(0, min(rdL, SCR_W));
        int wlL = max(0, rdL - wallW);
        if (wlR > wlL) spr.fillRect(wlL, subTop, wlR - wlL, subH, wCol);

        // Pared derecha: rellena desde borde der hasta el límite de la pantalla
        int wrL = max(0, min(rdR, SCR_W));
        int wrR = min(SCR_W, rdR + wallW);
        if (wrR > wrL) spr.fillRect(wrL, subTop, wrR - wrL, subH, wCol);

      } else if (!seg.tunnel) {
        int s1 = max(0, min(rmL, SCR_W));
        if (s1 > 0) spr.fillRect(0, subTop, s1, subH, grass);
        int s5 = max(0, min(rmR, SCR_W));
        if (s5 < SCR_W) spr.fillRect(s5, subTop, SCR_W - s5, subH, grass);

        int a2 = max(0, rmL), b2 = max(0, min(rdL, SCR_W));
        if (b2 > a2) spr.fillRect(a2, subTop, b2 - a2, subH, rumble);
        int a4 = max(0, rdR), b4 = min(SCR_W, rmR);
        if (b4 > a4) spr.fillRect(a4, subTop, b4 - a4, subH, rumble);
      }

      // Siempre carretera
      int a3 = max(0, rdL), b3 = min(SCR_W, rdR);
      if (b3 > a3) spr.fillRect(a3, subTop, b3 - a3, subH, road);
    }

    // Líneas de carril
    if (isLight && p0.w > 15 && bandH > 1) {
      int midX = (p0.x + p1.x) / 2;
      int midW = (p0.w + p1.w) / 2;
      int midY = drawTop + bandH / 2;
      int lh = min(bandH, 3);
      for (int l = 1; l < LANES; l++) {
        float lt = (float)l / LANES;
        int lx = (int)lerpF(midX - midW, midX + midW, lt);
        int lw = max(1, midW / 30);
        spr.fillRect(lx - lw / 2, midY, lw, lh, lane);
      }
    }
    maxy = drawTop;
  }

  // --- TERCER PASO: SPRITES Y TRÁFICO ENCIMA DE TODO ---
  for (int n = DRAW_DIST - 1; n > 1; n--) {
    int sIdx = (baseIdx + n) % TOTAL_SEGS;
    Segment& seg = segments[sIdx];
    RenderPt& p1 = rCache[n];

    if (p1.scale <= 0 || p1.y >= SCR_H) continue;

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
