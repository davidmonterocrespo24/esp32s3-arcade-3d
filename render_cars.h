/*
  ═══════════════════════════════════════════════════════════════
  RIVAL CARS IN THE ROAD PASS
  drawRoad() calls cars_prepare() once after its projection pass and
  cars_drawSlice() for every visible segment slice, back to front,
  right after the slice's scenery (with the slice's clip set). The
  player's car is drawn separately after the road (render_player).
  ═══════════════════════════════════════════════════════════════
*/

#ifndef RENDER_CARS_H
#define RENDER_CARS_H

void cars_prepare(float position);              // pick visible rivals and sort them far -> near
void cars_drawSlice(int n, float fog, bool tunnel);
// Rivals whose centre is nearer to the camera than the player's car are
// skipped by the road pass and drawn here, AFTER drawPlayerCar(), so they
// correctly cover it.
void cars_drawNear();

#endif // RENDER_CARS_H
