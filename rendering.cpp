/*
  ═══════════════════════════════════════════════════════════════
  IMPLEMENTACIÓN DE RENDERIZADO - MÓDULO PRINCIPAL
  Coordinador de submódulos de renderizado
  ═══════════════════════════════════════════════════════════════
*/

#include "rendering.h"
#include "config.h"
#include "colors.h"
#include "utils.h"

// Incluir submódulos
#include "render_player.h"
#include "render_traffic.h"
#include "render_road.h"
#include "render_hud.h"

// ═══════════════════════════════════════════════════════════════
//  VARIABLES GLOBALES (Definición)
// ═══════════════════════════════════════════════════════════════
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);

// -- PARALLAX BACKGROUND --
TFT_eSprite bgSpr = TFT_eSprite(&tft);
float skyOffset = 0.0f;
bool bgCreated = false;

RenderPt rCache[DRAW_DIST];
int16_t  rClip[DRAW_DIST];

// ═══════════════════════════════════════════════════════════════
//  FUNCIONES AUXILIARES
// ═══════════════════════════════════════════════════════════════

void initBackground() {
  // Crear sprite de fondo en PSRAM (DOBLE de ancho para seamless scrolling)
  // IMPORTANTE: Habilitar PSRAM _antes_ de crear el sprite
  bgSpr.setColorDepth(16);
  bgSpr.setAttribute(PSRAM_ENABLE, true);

  if (bgSpr.createSprite(SCR_W * 2, SCR_CY) == nullptr) {
    Serial.println("ERROR: Fallo al crear bgSpr en PSRAM!");
    bgCreated = false;
  } else {
    Serial.println("bgSpr creado en PSRAM correctamente.");
    bgCreated = true;
  }

  // 1. Dibujar cielo con degradado vertical (en todo el ancho)
  // Cielo estilo atardecer/ciudad: azul oscuro arriba a naranja/morado abajo
  for (int y = 0; y < SCR_CY; y++) {
    float t = (float)y / SCR_CY;
    uint16_t skyCol = lerpCol(rgb(40, 40, 80), rgb(150, 100, 150), t); // Azul oscuro a morado
    if (y > SCR_CY * 0.7) { // Última parte más naranja
       float t2 = (float)(y - SCR_CY * 0.7) / (SCR_CY * 0.3);
       skyCol = lerpCol(rgb(150, 100, 150), rgb(255, 180, 100), t2);
    }
    if (bgCreated) bgSpr.drawFastHLine(0, y, SCR_W * 2, skyCol);
  }

  // 2. Sol/Luna (opcional, dejamos sol poniente)
  int sunX = SCR_W;
  int sunY = SCR_CY - 15;
  if (bgCreated) {
    bgSpr.fillCircle(sunX, sunY, 20, rgb(255, 100, 50)); // Sol rojizo
    bgSpr.fillCircle(sunX, sunY, 15, rgb(255, 150, 50));
  }

  // 3. SKYLINE DE CIUDAD (Procedural)
  // Capa trasera (más oscura, edificios más bajos/lejanos)
  int x = 0;
  while (x < SCR_W * 2) {
      int w = random(10, 30);
      int h = random(20, 50);
      uint16_t buildCol = rgb(30, 30, 50); // Gris oscuro azulado
      if (bgCreated) bgSpr.fillRect(x, SCR_CY - h, w, h, buildCol);
      x += w;
  }

  // Capa delantera (más detallada, más alta)
  x = 0;
  while (x < SCR_W * 2) {
      int w = random(15, 40);
      int h = random(30, 80); // Edificios más altos
      uint16_t buildCol = rgb(20, 20, 40); // Casi negro

      // Edificio principal
      if (bgCreated) bgSpr.fillRect(x, SCR_CY - h, w, h, buildCol);

      // Ventanas (patrón simple)
      if (w > 10 && h > 10) {
          uint16_t winCol = rgb(80, 80, 100); // Ventanas tenues
          for (int wy = SCR_CY - h + 5; wy < SCR_CY - 2; wy += 8) {
              for (int wx = x + 3; wx < x + w - 3; wx += 6) {
                  if (random(0, 10) > 3) // 70% encendidas
                      if (bgCreated) bgSpr.drawPixel(wx, wy, winCol);
              }
          }
      }
      x += w;
  }
}
