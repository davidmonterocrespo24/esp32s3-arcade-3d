/*
  ═══════════════════════════════════════════════════════════════
  RENDERIZADO Y DIBUJO - MÓDULO PRINCIPAL
  Coordinador de submódulos de renderizado
  ═══════════════════════════════════════════════════════════════
*/

#ifndef RENDERING_H
#define RENDERING_H

#include <TFT_eSPI.h>
#include "config.h"
#include "structs.h"

// Incluir submódulos de renderizado
#include "render_player.h"
#include "render_traffic.h"
#include "render_road.h"
#include "render_hud.h"

// ═══════════════════════════════════════════════════════════════
//  VARIABLES GLOBALES DE RENDERIZADO
// ═══════════════════════════════════════════════════════════════
extern TFT_eSPI tft;
extern TFT_eSprite spr;

// -- PARALLAX BACKGROUND (Estilo Horizon Chase) --
extern TFT_eSprite bgSpr;
extern float skyOffset;
extern bool bgCreated;

extern RenderPt rCache[DRAW_DIST];
extern int16_t  rClip[DRAW_DIST];

// ═══════════════════════════════════════════════════════════════
//  FUNCIONES AUXILIARES
// ═══════════════════════════════════════════════════════════════

// Inicializar fondo parallax con skyline procedural
void initBackground();

// Dibujar un trapecio 3D (quad) - función auxiliar
void drawQuad(int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4, uint16_t c);

#endif // RENDERING_H
