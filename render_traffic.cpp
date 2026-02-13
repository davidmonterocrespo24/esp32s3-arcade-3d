/*
  ═══════════════════════════════════════════════════════════════
  IMPLEMENTACIÓN DE RENDERIZADO DE AUTOS DE TRÁFICO
  ═══════════════════════════════════════════════════════════════
*/

#include "render_traffic.h"
#include "rendering.h"
#include "config.h"
#include "colors.h"

void drawTrafficCar(int cx, int cy, float scale, uint16_t col, int16_t clipY) {
  // Verificación de escala mínima
  if (scale < 0.002f) return;
  if (cy >= SCR_H || cy < 0) return;

  // --- MOTOR 3D PARA AUTOS DE TRÁFICO ---
  // Siempre apuntan hacia adelante (hacia el horizonte)
  float angle = 0; // 0 radianes = mirando hacia +Z (fondo de la pista)
  float cosA = cos(angle);
  float sinA = sin(angle);

  // VÉRTICES DEL AUTO (versión simplificada del player)
  float verts[16][3] = {
    // --- CHASIS INFERIOR (0-7) ---
    {-18, 3, -40}, { 18, 3, -40}, { 18, 0,  40}, {-18, 0,  40},
    {-18, 11, -40}, { 18, 11, -40}, { 20, 10, 40}, {-20, 10, 40},

    // --- CABINA Y VIDRIOS (8-15) ---
    {-15, 11, -13}, { 15, 11, -13}, { 17, 11, 21}, {-17, 11, 21},
    {-12, 21,  -4}, { 12, 21,  -4}, { 12, 20, 13}, {-12, 20, 13}
  };

  float sx[16], sy[16];

  // Usamos directamente la escala que ya viene del sistema de renderizado
  // Esta escala ya tiene en cuenta la perspectiva de la carretera
  for (int i = 0; i < 16; i++) {
    // 1. Rotación sobre el eje Y (aunque angle=0, mantenemos la estructura)
    float rx = verts[i][0] * cosA - verts[i][2] * sinA;
    float ry = verts[i][1];

    // 2. Proyección simple: escalar y posicionar
    // scale ya contiene la perspectiva correcta (cameraDepth / camZ)
    // Multiplicamos por un factor grande para convertir a píxeles
    sx[i] = cx + (rx * scale * SCR_CX);
    sy[i] = cy - (ry * scale * SCR_CY);
  }

  // Backface Culling
  auto drawFace = [&](int v0, int v1, int v2, int v3, uint16_t faceCol) {
    float cross = (sx[v1] - sx[v0]) * (sy[v2] - sy[v0]) - (sy[v1] - sy[v0]) * (sx[v2] - sx[v0]);
    if (cross > 0) {
      // Evitar dibujar caras completamente fuera de pantalla
      float maxY = max(max(sy[v0], sy[v1]), max(sy[v2], sy[v3]));
      float minY = min(min(sy[v0], sy[v1]), min(sy[v2], sy[v3]));
      if (maxY < 0 || minY > SCR_H) return;
      drawQuad(sx[v0], sy[v0], sx[v1], sy[v1], sx[v2], sy[v2], sx[v3], sy[v3], faceCol);
    }
  };

  // Colores basados en el color del auto
  uint16_t hoodCol  = col;
  uint16_t bodyCol  = darkenCol(col, 0.85);
  uint16_t darkCol  = darkenCol(col, 0.65);
  uint16_t glassCol = rgb(80, 180, 255);
  uint16_t grillCol = rgb(30, 30, 30);

  // --- ORDEN DE DIBUJADO (De atrás hacia adelante para este ángulo) ---

  // Traseras primero (están más lejos de la cámara)
  drawFace(6, 7, 3, 2, darkCol);      // Parachoques Trasero
  drawFace(14, 15, 11, 10, grillCol); // Vidrio Trasero

  // Laterales y techo
  drawFace(7, 6, 5, 4, hoodCol);  // Cubierta Superior
  drawFace(7, 4, 0, 3, bodyCol);  // Lateral Izq
  drawFace(5, 6, 2, 1, bodyCol);  // Lateral Der

  // Cabina
  drawFace(15, 14, 13, 12, hoodCol);  // Techo
  drawFace(13, 14, 10, 9, bodyCol);   // Puerta Der
  drawFace(15, 12, 8, 11, bodyCol);   // Puerta Izq
  drawFace(12, 13, 9, 8, glassCol);   // Parabrisas

  // Frente (más cercano a la cámara del jugador)
  drawFace(0, 1, 2, 3, darkCol);  // Base Chasis
  drawFace(4, 5, 1, 0, grillCol); // Parrilla Frontal
}
