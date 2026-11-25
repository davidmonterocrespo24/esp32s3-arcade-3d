/*
  ═══════════════════════════════════════════════════════════════
  RENDERING - MAIN MODULE
  Coordinator of rendering submodules
  ═══════════════════════════════════════════════════════════════
*/

#ifndef RENDERING_H
#define RENDERING_H

#include "config.h"
#include "structs.h"
#include "gfx.h"
#include "colors.h"

#include "render_player.h"
#include "render_traffic.h"
#include "render_road.h"
#include "render_hud.h"

// Parallax background scroll (advanced by curves * speed)
extern float skyOffset;

// Generate the procedural skyline / stars / clouds (call once).
void initBackground();

// Sky gradient, sun/moon, stars, clouds and the two skyline layers.
void drawSky(float skyOffset, float timeSec);

#endif // RENDERING_H
