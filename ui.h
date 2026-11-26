/*
  ═══════════════════════════════════════════════════════════════
  UI SCREENS AND OVERLAYS
  Full screens (title, menu backdrop) draw the whole frame; overlays
  draw over the frame already in the framebuffer (race scene, menu
  backdrop). All strings are upper case (5x7 font: ASCII 32..95).
  ═══════════════════════════════════════════════════════════════
*/

#ifndef UI_H
#define UI_H

#include <stdint.h>
#include "race.h"

#define UI_COUNTDOWN_GO 4.0f      // seconds from the start of the countdown to GO

// ---- full screens ----
void ui_drawTitle(float t);                       // sky, showroom car, logo, PRESS START
void ui_drawMenuBackdrop(float t);                // showroom without the logo (behind menus)

// ---- overlays ----
// Vertical list: title + items, the selected one highlighted.
void ui_drawMenu(const char* title, const char* const* items, int count, int sel, float t);
// Circuit list + BACK (sel == NUM_CIRCUITS), with the selected circuit's
// tagline, laps, records and minimap (the circuit must be built).
void ui_drawCircuitSelect(int sel, float t);
// Start lights and 3-2-1 (t = seconds since the countdown began, GO at UI_COUNTDOWN_GO)
void ui_drawCountdown(float t);
void ui_drawGo(float sinceGo);                    // "GO!" fading out
void ui_drawPause(int sel);                       // RESUME / QUIT
void ui_drawFinish(float t, int place);           // FINISH banner + final place
// Results table (place order). points: per racer index, or nullptr outside a GP.
void ui_drawResults(const ResultRow* rows, const int* points, float t);
// GP standings: order = racer indices sorted by points; raceNo 1..3
void ui_drawStandings(const int* order, const int* points, int raceNo, int races, float t);
void ui_drawChampion(int winner, int points, float t);
void ui_drawDemoBanner(float t);
void ui_drawCrash(float sinceCrash);
void ui_drawBanner(const char* text, uint16_t color, float t);   // centred, slides in
void ui_fade(float t, float dur);                 // fade from black over [0, dur)
// Outline of the current circuit in a size x size box at (x, y); with
// racer dots (player last, on top) when dots is true.
void ui_drawMinimap(int x, int y, int size, bool dots);

// Time as M:SS.CC
void ui_fmtTime(char* out, int n, float t);

#endif // UI_H
