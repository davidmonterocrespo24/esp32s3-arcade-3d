/*
  ═══════════════════════════════════════════════════════════════
  RENDERING IMPLEMENTATION - MAIN MODULE
  Coordinator of rendering submodules
  ═══════════════════════════════════════════════════════════════
*/

#include "rendering.h"
#include "config.h"
#include "colors.h"
#include "utils.h"

// Include submodules
#include "render_player.h"
#include "render_traffic.h"
#include "render_road.h"
#include "render_hud.h"

// ═══════════════════════════════════════════════════════════════
//  GLOBAL VARIABLES (Definition)
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
//  HELPER FUNCTIONS
// ═══════════════════════════════════════════════════════════════

void initBackground() {
  // Create background sprite in PSRAM (DOUBLE width for seamless scrolling)
  // IMPORTANT: Enable PSRAM _before_ creating the sprite
  bgSpr.setColorDepth(16);
  bgSpr.setAttribute(PSRAM_ENABLE, true);

  if (bgSpr.createSprite(SCR_W * 2, SCR_CY) == nullptr) {
    Serial.println("ERROR: Failed to create bgSpr in PSRAM!");
    bgCreated = false;
  } else {
    Serial.println("bgSpr created in PSRAM successfully.");
    bgCreated = true;
  }

  // 1. Draw sky with vertical gradient (full width)
  // Sunset/city style sky: dark blue at top to orange/purple at bottom
  for (int y = 0; y < SCR_CY; y++) {
    float t = (float)y / SCR_CY;
    uint16_t skyCol = lerpCol(rgb(40, 40, 80), rgb(150, 100, 150), t); // Dark blue to purple
    if (y > SCR_CY * 0.7) { // Last part more orange
       float t2 = (float)(y - SCR_CY * 0.7) / (SCR_CY * 0.3);
       skyCol = lerpCol(rgb(150, 100, 150), rgb(255, 180, 100), t2);
    }
    if (bgCreated) bgSpr.drawFastHLine(0, y, SCR_W * 2, skyCol);
  }

  // 2. Sun/Moon (optional, we leave a setting sun)
  int sunX = SCR_W;
  int sunY = SCR_CY - 15;
  if (bgCreated) {
    bgSpr.fillCircle(sunX, sunY, 20, rgb(255, 100, 50)); // Reddish sun
    bgSpr.fillCircle(sunX, sunY, 15, rgb(255, 150, 50));
  }

  // 3. CITY SKYLINE (Procedural)
  // Back layer (darker, lower/more distant buildings)
  int x = 0;
  while (x < SCR_W * 2) {
      int w = random(10, 30);
      int h = random(20, 50);
      uint16_t buildCol = rgb(30, 30, 50); // Dark bluish grey
      if (bgCreated) bgSpr.fillRect(x, SCR_CY - h, w, h, buildCol);
      x += w;
  }

  // Front layer (more detailed, taller)
  x = 0;
  while (x < SCR_W * 2) {
      int w = random(15, 40);
      int h = random(30, 80); // Taller buildings
      uint16_t buildCol = rgb(20, 20, 40); // Nearly black

      // Main building
      if (bgCreated) bgSpr.fillRect(x, SCR_CY - h, w, h, buildCol);

      // Windows (simple pattern)
      if (w > 10 && h > 10) {
          uint16_t winCol = rgb(80, 80, 100); // Faint windows
          for (int wy = SCR_CY - h + 5; wy < SCR_CY - 2; wy += 8) {
              for (int wx = x + 3; wx < x + w - 3; wx += 6) {
                  if (random(0, 10) > 3) // 70% lit
                      if (bgCreated) bgSpr.drawPixel(wx, wy, winCol);
              }
          }
      }
      x += w;
  }
}
