/*
  ═══════════════════════════════════════════════════════════════
  IMPLEMENTACIÓN DE RENDERIZADO DEL AUTO DEL JUGADOR
  ═══════════════════════════════════════════════════════════════
*/

#include "render_player.h"
#include "rendering.h"
#include "config.h"
#include "colors.h"
#include "physics.h"

void drawPlayerCar() {
  int carCenterX = SCR_CX;
  int carCenterY = SCR_H - 15;

  // --- MOTOR 3D ---
  // Invertido: +playerX para que se incline al lado opuesto del giro
  float angle = (playerX * 0.22f) + PI;
  float cosA = cos(angle);
  float sinA = sin(angle);

  // VÉRTICES MODIFICADOS: Se aumentó el valor Y de la parte delantera (Z = -58)
  float verts[16][3] = {
    // --- CHASIS INFERIOR (0-7) ---
    {-26, 4, -58}, { 26, 4, -58}, { 26, 0,  58}, {-26, 0,  58}, // Frente levantado a Y=4
    {-26, 16, -58}, { 26, 16, -58}, { 28, 14, 58}, {-28, 14, 58}, // Capó a Y=16, maletero a Y=14

    // --- CABINA Y VIDRIOS (8-15) ---
    {-22, 16, -18}, { 22, 16, -18}, { 24, 15, 30}, {-24, 15, 30}, // Base de los vidrios
    {-17, 30,  -5}, { 17, 30,  -5}, { 17, 28, 18}, {-17, 28, 18}  // Techo levemente inclinado
  };

  float sx[16], sy[16];

  // Proyección
  for (int i = 0; i < 16; i++) {
    float rx = verts[i][0] * cosA - verts[i][2] * sinA;
    float ry = verts[i][1];
    float rz = verts[i][0] * sinA + verts[i][2] * cosA;

    float pitch = -0.15f;
    float camY = ry * cos(pitch) - rz * sin(pitch);
    float camZ = ry * sin(pitch) + rz * cos(pitch);

    camZ += 200.0f;
    float fov = 200.0f;

    sx[i] = carCenterX + (rx * fov) / camZ;
    sy[i] = carCenterY - (camY * fov) / camZ;
  }

  // Backface Culling
  auto drawFace = [&](int v0, int v1, int v2, int v3, uint16_t col) {
    float cross = (sx[v1] - sx[v0]) * (sy[v2] - sy[v0]) - (sy[v1] - sy[v0]) * (sx[v2] - sx[v0]);
    if (cross > 0) {
      drawQuad(sx[v0], sy[v0], sx[v1], sy[v1], sx[v2], sy[v2], sx[v3], sy[v3], col);
    }
  };

  // Colores
  uint16_t hoodRed  = rgb(255, 60, 60);
  uint16_t bodyRed  = rgb(210, 30, 30);
  uint16_t darkRed  = rgb(140, 20, 20);
  uint16_t glassCol = rgb(80, 180, 255);
  uint16_t grillCol = rgb(30, 30, 30);

  // --- ORDEN DE DIBUJADO CORRECTO (De adelante hacia atrás) ---

  // 1. Frente y Base
  drawFace(4, 5, 1, 0, grillCol); // Parrilla Frontal
  drawFace(0, 1, 2, 3, darkRed);  // Base Chasis

  // 2. Laterales y Capó
  drawFace(7, 4, 0, 3, bodyRed);  // Lateral Izq
  drawFace(5, 6, 2, 1, bodyRed);  // Lateral Der
  drawFace(7, 6, 5, 4, hoodRed);  // Cubierta Superior (Maletero/Capó)

  // 3. Cabina (Interiores)
  drawFace(12, 13, 9, 8, glassCol);   // Parabrisas
  drawFace(15, 12, 8, 11, bodyRed);   // Puerta Izq
  drawFace(13, 14, 10, 9, bodyRed);   // Puerta Der
  drawFace(15, 14, 13, 12, hoodRed);  // Techo

  // 4. Traseras (Lo que está más pegado a la cámara)
  drawFace(14, 15, 11, 10, grillCol); // Vidrio Trasero
  drawFace(6, 7, 3, 2, darkRed);      // Parachoques Trasero
}

void drawStartScreen(float time) {
  spr.fillSprite(TFT_BLACK);

  // --- TEXTOS DE LA PANTALLA ---
  spr.fillRect(0, 100, SCR_W, 3, TFT_RED);
  spr.fillRect(0, 105, SCR_W, 3, TFT_WHITE);

  spr.setTextColor(TFT_RED);
  spr.setTextSize(3);
  spr.setCursor(22, 115);
  spr.print("OUTRUN ESP32");

  spr.setTextSize(2);
  spr.setTextColor(TFT_YELLOW);
  spr.setCursor(60, 145);
  spr.print("3D RACING");

  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE);
  spr.setCursor(30, 170);
  spr.print("LEFT / RIGHT buttons to steer");
  spr.setCursor(30, 185);
  spr.print("Car accelerates automatically");

  // --- SOMBRA (Dibuja el suelo debajo del coche) ---
  int carCenterX = SCR_CX;
  int carCenterY = 85;
  spr.fillEllipse(carCenterX, carCenterY + 40, 55, 18, rgb(15, 15, 15));

  // --- MOTOR 3D REAL ---
  float angle = time * 1.5f; // Velocidad de giro
  float cosA = cos(angle);
  float sinA = sin(angle);

  // Esculpimos el coche: 16 Vértices {X (Ancho), Y (Alto), Z (Largo)}
  float verts[16][3] = {
    // --- CHASIS INFERIOR (0-7) ---
    {-22, 0, -48}, { 22, 0, -48}, { 22, 0,  48}, {-22, 0,  48}, // 0-3: Base pegada al piso
    {-22, 8, -48}, { 22, 8, -48}, { 24, 14, 48}, {-24, 14, 48}, // 4-7: Capó inclinado hacia abajo y maletero

    // --- CABINA Y VIDRIOS (8-15) ---
    {-18, 10, -15}, { 18, 10, -15}, { 20, 14, 25}, {-20, 14, 25}, // 8-11: Base de los vidrios
    {-14, 24,  0},  { 14, 24,  0},  { 14, 22, 15}, {-14, 22, 15}  // 12-15: Techo de la cabina
  };

  float sx[16], sy[16];

  // Matemáticas de Proyección
  for (int i = 0; i < 16; i++) {
    // 1. Rotación sobre el eje Y (Giro del auto)
    float rx = verts[i][0] * cosA - verts[i][2] * sinA;
    float ry = verts[i][1];
    float rz = verts[i][0] * sinA + verts[i][2] * cosA;

    // 2. Inclinación de la cámara (Mirar ligeramente desde arriba)
    float pitch = 0.4f;
    float camY = ry * cos(pitch) - rz * sin(pitch);
    float camZ = ry * sin(pitch) + rz * cos(pitch);

    // 3. Alejar el objeto y aplicar la división de perspectiva
    camZ += 150.0f;
    float fov = 160.0f;

    // Proyección final al monitor 2D
    sx[i] = carCenterX + (rx * fov) / camZ;
    sy[i] = carCenterY - (camY * fov) / camZ;
  }

  // --- LÓGICA DE DIBUJADO (CULLING) ---
  // Calcula el producto cruzado. Si los vértices giran en sentido horario en la pantalla, la cara es visible.
  auto drawFace = [&](int v0, int v1, int v2, int v3, uint16_t col) {
    float cross = (sx[v1] - sx[v0]) * (sy[v2] - sy[v0]) - (sy[v1] - sy[v0]) * (sx[v2] - sx[v0]);
    if (cross > 0) {
      drawQuad(sx[v0], sy[v0], sx[v1], sy[v1], sx[v2], sy[v2], sx[v3], sy[v3], col);
    }
  };

  // Paleta de colores para el sombreado falso
  uint16_t hoodRed  = rgb(255, 60, 60);  // Rojo iluminado (Capó y Techo)
  uint16_t bodyRed  = rgb(210, 30, 30);  // Rojo lateral
  uint16_t darkRed  = rgb(140, 20, 20);  // Rojo oscuro (Atrás)
  uint16_t glassCol = rgb(80, 180, 255); // Azul cielo (Parabrisas)
  uint16_t grillCol = rgb(30, 30, 30);   // Gris oscuro (Parrilla y vidrio trasero)

  // 1. Dibujamos el cuerpo principal primero
  drawFace(0, 1, 2, 3, darkRed);  // Base del chasis
  drawFace(7, 4, 0, 3, bodyRed);  // Lateral Izquierdo
  drawFace(5, 6, 2, 1, bodyRed);  // Lateral Derecho
  drawFace(6, 7, 3, 2, darkRed);  // Parte Trasera
  drawFace(4, 5, 1, 0, grillCol); // Frontal (Parrilla oscura)
  drawFace(7, 6, 5, 4, hoodRed);  // Capó y maletero

  // 2. Dibujamos la cabina encima (El orden garantiza que no haya glitches)
  drawFace(15, 12, 8, 11, bodyRed);   // Puerta izquierda
  drawFace(13, 14, 10, 9, bodyRed);   // Puerta derecha
  drawFace(14, 15, 11, 10, grillCol); // Vidrio trasero
  drawFace(12, 13, 9, 8, glassCol);   // Parabrisas frontal azul
  drawFace(15, 14, 13, 12, hoodRed);  // Techo rojo

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
