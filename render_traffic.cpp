/*
  ═══════════════════════════════════════════════════════════════
  TRAFFIC CAR RENDERING IMPLEMENTATION
  ═══════════════════════════════════════════════════════════════
*/

#include "render_traffic.h"
#include "rendering.h"
#include "config.h"
#include "colors.h"

void drawTrafficCar(int cx, int cy, float scale, uint16_t col, int16_t clipY) {
  // Minimum scale check
  if (scale < 0.002f) return;
  if (cy >= SCR_H || cy < 0) return;

  // --- 3D ENGINE FOR TRAFFIC CARS ---
  // Always pointing forward (toward the horizon)
  float angle = 0; // 0 radians = facing +Z (back of the track)
  float cosA = cos(angle);
  float sinA = sin(angle);

  // CAR VERTICES (simplified version of player)
  float verts[16][3] = {
    // --- LOWER CHASSIS (0-7) ---
    {-18, 3, -40}, { 18, 3, -40}, { 18, 0,  40}, {-18, 0,  40},
    {-18, 11, -40}, { 18, 11, -40}, { 20, 10, 40}, {-20, 10, 40},

    // --- CABIN AND WINDOWS (8-15) ---
    {-15, 11, -13}, { 15, 11, -13}, { 17, 11, 21}, {-17, 11, 21},
    {-12, 21,  -4}, { 12, 21,  -4}, { 12, 20, 13}, {-12, 20, 13}
  };

  float sx[16], sy[16];

  // Use the scale directly from the rendering system
  // This scale already accounts for the road perspective
  for (int i = 0; i < 16; i++) {
    // 1. Rotation around Y axis (even with angle=0, keep the structure)
    float rx = verts[i][0] * cosA - verts[i][2] * sinA;
    float ry = verts[i][1];

    // 2. Simple projection: scale and position
    // scale already contains the correct perspective (cameraDepth / camZ)
    // Multiply by a large factor to convert to pixels
    sx[i] = cx + (rx * scale * SCR_CX);
    sy[i] = cy - (ry * scale * SCR_CY);
  }

  // Backface Culling
  auto drawFace = [&](int v0, int v1, int v2, int v3, uint16_t faceCol) {
    float cross = (sx[v1] - sx[v0]) * (sy[v2] - sy[v0]) - (sy[v1] - sy[v0]) * (sx[v2] - sx[v0]);
    if (cross > 0) {
      // Avoid drawing faces completely off screen
      float maxY = max(max(sy[v0], sy[v1]), max(sy[v2], sy[v3]));
      float minY = min(min(sy[v0], sy[v1]), min(sy[v2], sy[v3]));
      if (maxY < 0 || minY > SCR_H) return;
      drawQuad(sx[v0], sy[v0], sx[v1], sy[v1], sx[v2], sy[v2], sx[v3], sy[v3], faceCol);
    }
  };

  // Colors based on car color
  uint16_t hoodCol  = col;
  uint16_t bodyCol  = darkenCol(col, 0.85);
  uint16_t darkCol  = darkenCol(col, 0.65);
  uint16_t glassCol = rgb(80, 180, 255);
  uint16_t grillCol = rgb(30, 30, 30);

  // --- DRAW ORDER (Back to front for this angle) ---

  // Rear first (farthest from camera)
  drawFace(6, 7, 3, 2, darkCol);      // Rear Bumper
  drawFace(14, 15, 11, 10, grillCol); // Rear Window

  // Sides and roof
  drawFace(7, 6, 5, 4, hoodCol);  // Top Cover
  drawFace(7, 4, 0, 3, bodyCol);  // Left Side
  drawFace(5, 6, 2, 1, bodyCol);  // Right Side

  // Cabin
  drawFace(15, 14, 13, 12, hoodCol);  // Roof
  drawFace(13, 14, 10, 9, bodyCol);   // Right Door
  drawFace(15, 12, 8, 11, bodyCol);   // Left Door
  drawFace(12, 13, 9, 8, glassCol);   // Windshield

  // Front (closest to player camera)
  drawFace(0, 1, 2, 3, darkCol);  // Chassis Base
  drawFace(4, 5, 1, 0, grillCol); // Front Grille
}
