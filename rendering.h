/*
  ═══════════════════════════════════════════════════════════════
  SKY AND PARALLAX BACKGROUND
  ═══════════════════════════════════════════════════════════════
*/

#ifndef RENDERING_H
#define RENDERING_H

#include "config.h"
#include "gfx.h"
#include "colors.h"

// Parallax background scroll (advanced by curves * speed)
extern float skyOffset;

// Seconds of the last drawSky() call: the animation clock of the
// background and of the scenery sprites (flags, beams, blinking lights).
extern float bgTime;

// Generate the background of a circuit theme (Theme in circuits.h):
// city skyline / sea horizon with islands / mountain ranges, stars and
// clouds. Deterministic per theme. Call once per circuit (buildCircuit).
void initBackground(int theme);

// Sky gradient, sun/moon, stars, clouds and the background layers of the
// theme chosen at init, with the current palette.
void drawSky(float skyOffset, float timeSec);

#endif // RENDERING_H
