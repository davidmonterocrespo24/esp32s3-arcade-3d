/*
  ═══════════════════════════════════════════════════════════════
  IMPLEMENTACIÓN DE RENDERIZADO DE AUTOS DE TRÁFICO
  ═══════════════════════════════════════════════════════════════
*/

#include "render_traffic.h"
#include "rendering.h"
#include "config.h"
#include "colors.h"

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
