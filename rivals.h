/*
  ═══════════════════════════════════════════════════════════════
  RIVAL AI
  Each rival follows a racing line derived from the curvature ahead,
  with its own pace and line preference, keeps a gap to the car ahead
  and overtakes on the free side. No drift physics: rivals steer
  straight towards their target line with a limited lateral rate.
  ═══════════════════════════════════════════════════════════════
*/

#ifndef RIVALS_H
#define RIVALS_H

void rivals_init();            // per-rival skill, line preferences, start reaction
void rivals_update(float dt);  // drive the rivals (idle until race_go)

// Shared driving helpers (the player's autopilot uses them too)
// Racing-line x (road half-widths) at track position z, plus a lateral bias.
float ai_lineTarget(float z, float bias);
// Fraction of maxSpeed the corners just ahead of z allow (0.6 .. 1).
float ai_cornerSpeed(float z);

#endif // RIVALS_H
