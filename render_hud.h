/*
  ═══════════════════════════════════════════════════════════════
  HUD AND SPEEDOMETER RENDERING
  ═══════════════════════════════════════════════════════════════
*/

#ifndef RENDER_HUD_H
#define RENDER_HUD_H

// fps: game frames per second, lcdFps: frames actually shown on the panel,
// frameMs: CPU time spent producing the last frame.
void drawHUD(float timeSec, float fps, float lcdFps, float frameMs);

#endif // RENDER_HUD_H
