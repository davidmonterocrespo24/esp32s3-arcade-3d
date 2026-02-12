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

  // Si estamos en túnel, NO dibujar nada aquí - el techo se dibuja en el loop 3D
  if (segments[pSegIdx].tunnel) {
    // Dejar vacío para que el techo 3D sea visible
    return;
  }

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

void drawBuilding(RenderPt& p0, RenderPt& p1, int heightVal, uint16_t baseCol, int sIdx, bool isLeft, bool showFront) {
  int h1 = (int)(p1.scale * heightVal);
  int h0 = (int)(p0.scale * heightVal);
  // Ancho del edificio MUCHO MÁS ANCHO (400000)
  int bw1 = (int)(p1.scale * 400000);
  int bw0 = (int)(p0.scale * 400000);

  // Offset desde la carretera (1.5x validado por usuario)
  int off1 = p1.w * 1.5;
  int off0 = p0.w * 1.5;

  // Coordenadas base (izq o derecha)
  // Si es izquierda: restamos offset. Si es derecha: sumamos.
  int x0_side = isLeft ? (p0.x - off0) : (p0.x + off0);
  int x1_side = isLeft ? (p1.x - off1) : (p1.x + off1);

  int x0_outer = isLeft ? (x0_side - bw0) : (x0_side + bw0);
  int x1_outer = isLeft ? (x1_side - bw1) : (x1_side + bw1);

  // 1. PARED LATERAL (La que da a la carretera)
  // Oscurecer un poco para dar volumen
  uint16_t sideCol = darkenCol(baseCol, 0.6);
  drawQuad(x0_side, p0.y, x1_side, p1.y,
           x1_side, p1.y - h1, x0_side, p0.y - h0, sideCol);

  // 2. DETALLES / VENTANAS (Según Estilo)
  int style = sIdx % 6;
  int numFloors = h0 / 25; // Pisos aprox

  if (numFloors > 1 && numFloors < 30) {
     if (style == 0) { // ESTILO 0: STANDARD (Oficinas)
       uint16_t winCol = rgb(220, 220, 180); // Luz cálida
       for (int fl = 1; fl < numFloors; fl++) {
         float t = (float)fl / numFloors;
         int wy0 = p0.y - (int)(h0 * t);
         int wy1 = p1.y - (int)(h1 * t);
         // Líneas horizontales simples
         spr.drawLine(x0_side, wy0, x1_side, wy1, winCol);
       }
     }
     else if (style == 1) { // ESTILO 1: TORRE DE CRISTAL (Azulada)
       uint16_t glassCol = rgb(100, 200, 255);
       // Líneas verticales reflejantes
       int midX0 = (x0_side + x0_outer) / 2; // (Aprox, solo dibujamos en la cara lateral por ahora)
       spr.drawLine(x0_side, p0.y - h0/2, x1_side, p1.y - h1/2, glassCol);
       // Refuerzo borde
       spr.drawLine(x0_side, p0.y - h0, x1_side, p1.y - h1, TFT_WHITE);
     }
     else if (style == 2) { // ESTILO 2: RESIDENCIAL (Ladrillo/Naranja)
       uint16_t winCol = TFT_YELLOW;
       // Ventanas cuadradas dispersas
       for (int fl = 1; fl < numFloors; fl++) {
         if ((fl + sIdx) % 2 == 0) continue; // Alternar pisos
         float t = (float)fl / numFloors;
         int wy0 = p0.y - (int)(h0 * t);
         int wy1 = p1.y - (int)(h1 * t);
         spr.drawLine(x0_side, wy0, x1_side, wy1, winCol);
       }
     }
     else if (style == 3) { // ESTILO 3: MODERNO (Blanco/Negro)
       uint16_t winCol = TFT_WHITE;
       // Pocas líneas, muy finas (minimalista)
       if (numFloors > 5) {
         float t = 0.8;
         int wy0 = p0.y - (int)(h0 * t);
         int wy1 = p1.y - (int)(h1 * t);
         spr.drawLine(x0_side, wy0, x1_side, wy1, winCol);
       }
     }
     else if (style == 4) { // ESTILO 4: INDUSTRIAL (Oscuro)
        // Franjas de precaución o luces rojas
        if (numFloors > 2) {
           float t = 0.9; // Luz de obstrucción aerea
           int wy0 = p0.y - (int)(h0 * t);
           int wy1 = p1.y - (int)(h1 * t);
           spr.drawCircle((x0_side+x1_side)/2, (wy0+wy1)/2, 2, TFT_RED);
        }
     }
     else if (style == 5) { // ESTILO 5: NOCTURNO / NEON
        uint16_t neonCol = (sIdx % 2 == 0) ? rgb(255, 0, 255) : rgb(0, 255, 255);
        // Borde neón vertical
        spr.drawLine(x0_side, p0.y, x0_side, p0.y - h0, neonCol);
     }
  }

  // 3. TECHO
  int roX1 = isLeft ? (x1_outer) : (x1_side); // Esquinas exteriores lejanas
  int roX0 = isLeft ? (x0_outer) : (x0_side); // Esquinas exteriores cercanas
  // Ajuste coord techo para quad
  drawQuad(x0_side, p0.y - h0, x1_side, p1.y - h1,
           x1_outer, p1.y - h1, x0_outer, p0.y - h0,
           darkenCol(baseCol, 0.85));

  // 4. FACHADA FRONTAL (Solo si es visible y segura)
  if (showFront) {
    drawQuad(x0_side, p0.y, x0_outer, p0.y,
             x0_outer, p0.y - h0, x0_side, p0.y - h0,
             baseCol);

    // Detalle puerta/entrada en fachada estándar
    if (h0 > 15 && bw0 > 10) {
       uint16_t doorCol = rgb(20, 20, 20);
       int doorH = h0 / 5;
       int doorW = bw0 / 3;
       int doorX = isLeft ? (x0_side - bw0/2 - doorW/2) : (x0_side + bw0/2 - doorW/2);
       spr.fillRect(doorX, p0.y - doorH, doorW, doorH, doorCol);
    }
  }
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

    // --- TECHO DEL TÚNEL (Mismo cálculo que las paredes para alineación) ---
    if (seg.tunnel) {
      // Usar la MISMA altura que las paredes del túnel
      float ceilHeight = 9000.0f;

      // Calcular coordenadas Y del techo (igual que en las paredes, líneas 366-368)
      float cyp1 = (p1Y + ceilHeight) - camY;
      float cyp2 = (p2Y + ceilHeight) - camY;
      int16_t csy1 = SCR_CY - (int)(sc1 * cyp1 * SCR_CY);
      int16_t csy2 = SCR_CY - (int)(sc2 * cyp2 * SCR_CY);

      int ceilDrawTop = max((int)csy2, 0);
      int ceilDrawBot = min((int)csy1, SCR_CY);
      int ceilBandH = ceilDrawBot - ceilDrawTop;

      if (ceilBandH > 0) {
        int ceilSubDiv = (ceilBandH > 6) ? min(3, ceilBandH / 3) : 1;

        for (int s = 0; s < ceilSubDiv; s++) {
          int ceilSubTop = ceilDrawTop + s * ceilBandH / ceilSubDiv;
          int ceilSubBot = ceilDrawTop + (s + 1) * ceilBandH / ceilSubDiv;
          int ceilSubH = ceilSubBot - ceilSubTop;
          if (ceilSubH <= 0) continue;

          // MISMA interpolación que la carretera
          float tMid = (float)(2 * s + 1) / (float)(2 * ceilSubDiv);
          int cx = (int)lerpF(sx2, sx1, tMid);
          int hw = (int)lerpF(sw2, sw1, tMid);
          int ceilL = cx - hw, ceilR = cx + hw;

          // Dibujar techo (mismo color que carretera)
          int a3 = max(0, ceilL), b3 = min(SCR_W, ceilR);
          if (b3 > a3) spr.fillRect(a3, ceilSubTop, b3 - a3, ceilSubH, road);
        }
      }
    }

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

    // TÚNEL EN 3D (BACK-TO-FRONT) - Solo paredes, el techo ya se dibujó en el primer loop
    if (seg.tunnel) {
       bool isLightT = ((sIdx / 3) % 2) == 0;
       uint16_t wallT = isLightT ? rgb(80, 120, 200) : rgb(50, 80, 150);  // Azul

       float cH = 9000.0f;
       int cy1 = SCR_CY - (int)(p1.scale * (seg.y + cH - camY) * SCR_CY);
       int cy0 = SCR_CY - (int)(p0.scale * (prevSeg.y + cH - camY) * SCR_CY);

       // Bordes de la carretera (suelo)
       int roadL0 = p0.x - p0.w;
       int roadR0 = p0.x + p0.w;
       int roadL1 = p1.x - p1.w;
       int roadR1 = p1.x + p1.w;

       // Bordes del techo
       int ceilL0 = roadL0;
       int ceilR0 = roadR0;
       int ceilL1 = roadL1;
       int ceilR1 = roadR1;

       // 1. Pared Izquierda VERTICAL
       drawQuad(roadL0, p0.y, roadL1, p1.y, ceilL1, cy1, ceilL0, cy0, wallT);

       // 2. Pared Derecha VERTICAL
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
        // En el túnel: NO dibujar paredes en la zona del techo (arriba de SCR_CY)
        // Solo dibujar las paredes en la zona inferior (desde SCR_CY hacia abajo)
        int wallDrawTop = max(subTop, SCR_CY);
        int wallDrawBot = subBot;
        int wallH = wallDrawBot - wallDrawTop;

        if (wallH > 0 && !seg.tunnel) {
          if (rdL > 0) spr.fillRect(0, wallDrawTop, rdL, wallH, grass);
          if (rdR < SCR_W) spr.fillRect(rdR, wallDrawTop, SCR_W - rdR, wallH, grass);
        }
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
