/*
  ═══════════════════════════════════════════════════════════════
  TRAFFIC CAR RENDERING IMPLEMENTATION
  ═══════════════════════════════════════════════════════════════
*/

#include "render_traffic.h"
#include "rendering.h"
#include "config.h"
#include "colors.h"

void drawBuilding(RenderPt& p0, RenderPt& p1, int heightVal, uint16_t baseCol, int sIdx, bool isLeft, bool showFront) {
  int h1 = (int)(p1.scale * heightVal);
  int h0 = (int)(p0.scale * heightVal);
  // Building width MUCH WIDER (400000)
  int bw1 = (int)(p1.scale * BUILDING_W);
  int bw0 = (int)(p0.scale * BUILDING_W);

  int off1 = p1.w * BUILDING_OFFSET;
  int off0 = p0.w * BUILDING_OFFSET;

  // Base coordinates (left or right side)
  // If left: subtract offset. If right: add.
  int x0_side = isLeft ? (p0.x - off0) : (p0.x + off0);
  int x1_side = isLeft ? (p1.x - off1) : (p1.x + off1);

  int x0_outer = isLeft ? (x0_side - bw0) : (x0_side + bw0);
  int x1_outer = isLeft ? (x1_side - bw1) : (x1_side + bw1);

  // 1. SIDE WALL (The one facing the road)
  // Darken slightly to give volume
  uint16_t sideCol = darkenCol(baseCol, 0.6);
  drawQuad(x0_side, p0.y, x1_side, p1.y,
           x1_side, p1.y - h1, x0_side, p0.y - h0, sideCol);

  // 2. DETAILS / WINDOWS (By Style)
  int style = sIdx % 6;
  int numFloors = h0 / 25; // Approximate floors

  if (numFloors > 1 && numFloors < 30) {
     if (style == 0) { // STYLE 0: STANDARD (Offices)
       uint16_t winCol = rgb(220, 220, 180); // Warm light
       for (int fl = 1; fl < numFloors; fl++) {
         float t = (float)fl / numFloors;
         int wy0 = p0.y - (int)(h0 * t);
         int wy1 = p1.y - (int)(h1 * t);
         // Simple horizontal lines
         spr.drawLine(x0_side, wy0, x1_side, wy1, winCol);
       }
     }
     else if (style == 1) { // STYLE 1: GLASS TOWER (Bluish)
       uint16_t glassCol = rgb(100, 200, 255);
       // Reflective vertical lines
       int midX0 = (x0_side + x0_outer) / 2; // (Approx, only drawing on side face for now)
       spr.drawLine(x0_side, p0.y - h0/2, x1_side, p1.y - h1/2, glassCol);
       // Edge reinforcement
       spr.drawLine(x0_side, p0.y - h0, x1_side, p1.y - h1, TFT_WHITE);
     }
     else if (style == 2) { // STYLE 2: RESIDENTIAL (Brick/Orange)
       uint16_t winCol = TFT_YELLOW;
       // Scattered square windows
       for (int fl = 1; fl < numFloors; fl++) {
         if ((fl + sIdx) % 2 == 0) continue; // Alternate floors
         float t = (float)fl / numFloors;
         int wy0 = p0.y - (int)(h0 * t);
         int wy1 = p1.y - (int)(h1 * t);
         spr.drawLine(x0_side, wy0, x1_side, wy1, winCol);
       }
     }
     else if (style == 3) { // STYLE 3: MODERN (White/Black)
       uint16_t winCol = TFT_WHITE;
       // Few lines, very thin (minimalist)
       if (numFloors > 5) {
         float t = 0.8;
         int wy0 = p0.y - (int)(h0 * t);
         int wy1 = p1.y - (int)(h1 * t);
         spr.drawLine(x0_side, wy0, x1_side, wy1, winCol);
       }
     }
     else if (style == 4) { // STYLE 4: INDUSTRIAL (Dark)
        // Warning stripes or red lights
        if (numFloors > 2) {
           float t = 0.9; // Aerial obstruction light
           int wy0 = p0.y - (int)(h0 * t);
           int wy1 = p1.y - (int)(h1 * t);
           spr.drawCircle((x0_side+x1_side)/2, (wy0+wy1)/2, 2, TFT_RED);
        }
     }
     else if (style == 5) { // STYLE 5: NIGHT / NEON
        uint16_t neonCol = (sIdx % 2 == 0) ? rgb(255, 0, 255) : rgb(0, 255, 255);
        // Vertical neon edge
        spr.drawLine(x0_side, p0.y, x0_side, p0.y - h0, neonCol);
     }
  }

  // 3. ROOF
  int roX1 = isLeft ? (x1_outer) : (x1_side); // Far outer corners
  int roX0 = isLeft ? (x0_outer) : (x0_side); // Near outer corners
  // Adjust roof coords for quad
  drawQuad(x0_side, p0.y - h0, x1_side, p1.y - h1,
           x1_outer, p1.y - h1, x0_outer, p0.y - h0,
           darkenCol(baseCol, 0.85));

  // 4. FRONT FACADE (Only if visible and safe)
  if (showFront) {
    drawQuad(x0_side, p0.y, x0_outer, p0.y,
             x0_outer, p0.y - h0, x0_side, p0.y - h0,
             baseCol);

    // Door/entrance detail on standard facade
    if (h0 > 15 && bw0 > 10) {
       uint16_t doorCol = rgb(20, 20, 20);
       int doorH = h0 / 5;
       int doorW = bw0 / 3;
       int doorX = isLeft ? (x0_side - bw0/2 - doorW/2) : (x0_side + bw0/2 - doorW/2);
       spr.fillRect(doorX, p0.y - doorH, doorW, doorH, doorCol);
    }
  }
}
