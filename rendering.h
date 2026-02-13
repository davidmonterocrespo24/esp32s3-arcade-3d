/*
  ═══════════════════════════════════════════════════════════════
  RENDERING AND DRAWING - MAIN MODULE
  Coordinator of rendering submodules
  ═══════════════════════════════════════════════════════════════
*/

#ifndef RENDERING_H
#define RENDERING_H

#include <TFT_eSPI.h>
#include "config.h"
#include "structs.h"

// Include rendering submodules
#include "render_player.h"
#include "render_traffic.h"
#include "render_road.h"
#include "render_hud.h"

// ═══════════════════════════════════════════════════════════════
//  GLOBAL RENDERING VARIABLES
// ═══════════════════════════════════════════════════════════════
extern TFT_eSPI tft;
extern TFT_eSprite spr;

// -- PARALLAX BACKGROUND (Horizon Chase style) --
extern TFT_eSprite bgSpr;
extern float skyOffset;
extern bool bgCreated;

extern RenderPt rCache[DRAW_DIST];
extern int16_t  rClip[DRAW_DIST];

// ═══════════════════════════════════════════════════════════════
//  HELPER FUNCTIONS
// ═══════════════════════════════════════════════════════════════

// Initialize parallax background with procedural skyline
void initBackground();

// Draw a 3D trapezoid (quad) - helper function
void drawQuad(int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4, uint16_t c);

#endif // RENDERING_H
