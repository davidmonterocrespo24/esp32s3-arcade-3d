/*
  ═══════════════════════════════════════════════════════════════
  UI SCREENS AND OVERLAYS
  Full screens (title, menu backdrop) draw the whole frame; overlays
  draw over the frame already in the framebuffer (race scene, menu
  backdrop). All strings are upper case (5x7 font: ASCII 32..95).

  One look everywhere: chamfered translucent panels with an accent
  edge, an animated selection bar, and the 5x7 font drawn as spans
  with an outline, a drop shadow, an optional italic shear and a
  vertical colour gradient (ui_text). The HUD (render_hud.cpp) uses
  the same toolkit.
  ═══════════════════════════════════════════════════════════════
*/

#ifndef UI_H
#define UI_H

#include <stdint.h>
#include "race.h"

#define UI_COUNTDOWN_GO 4.0f      // seconds from the start of the countdown to GO
#define UI_TITLE_FEATURE_TIME 5.0f   // the title features each circuit this long

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
void ui_drawPause(int sel);                       // RESUME / RESTART / QUIT
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

// ═══════════════════════════════════════════════════════════════
//  ADDITIONS (UI phase) — toolkit shared with the HUD
// ═══════════════════════════════════════════════════════════════

// Colours of the UI theme (RGB565)
#define UI_RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define UI_INK      UI_RGB(6, 8, 24)        // panel tint
#define UI_EDGE     UI_RGB(70, 84, 140)     // panel border
#define UI_ACCENT   UI_RGB(255, 56, 104)    // hot pink-red: selection, rules
#define UI_ACCENT2  UI_RGB(56, 206, 255)    // cyan: secondary info
#define UI_GOLD     UI_RGB(255, 206, 52)
#define UI_SILVER   UI_RGB(206, 214, 230)
#define UI_BRONZE   UI_RGB(222, 140, 70)
#define UI_TEXT     UI_RGB(238, 240, 250)
#define UI_DIM      UI_RGB(146, 152, 180)
#define UI_GOOD     UI_RGB(84, 244, 128)
#define UI_BAD      UI_RGB(255, 76, 64)
#define UI_PURPLE   UI_RGB(204, 100, 255)  // fastest lap
#define UI_SHADOW   UI_RGB(0, 0, 10)

// Text drawn as spans: every font dot is a scale x scale block (scale may be
// fractional), grown by outlinePx for the outline / shadow, sheared by slant
// (x shift per pixel of height, the bottom row stays put) and filled with a
// vertical gradient g[0] (top) -> g[1] (middle) | g[2] (middle) -> g[3] (bottom).
struct UiTextStyle {
  float    scale;
  float    slant;
  int8_t   spacing;          // extra pixels between characters
  uint8_t  outlinePx;        // 0 = no outline
  int8_t   shadowDx, shadowDy;   // 0,0 = no shadow
  uint8_t  alpha;            // 32 = opaque, less blends the whole text
  uint16_t g[4];
  uint16_t outline, shadow;
};
enum { UI_LEFT = 0, UI_CENTER = 1, UI_RIGHT = 2 };

UiTextStyle ui_style(float scale, uint16_t color);                  // flat colour, 1 px outline
UiTextStyle ui_styleGrad(float scale, uint16_t top, uint16_t bottom);  // gradient + outline + shadow
UiTextStyle ui_styleChrome(float scale);                            // 80s chrome headline
UiTextStyle ui_styleGold(float scale);
int  ui_textWidth(const char* s, float scale, int spacing = 0);
// (x, y): top-left / top-centre / top-right of the glyph box (align)
void ui_text(float x, float y, const char* s, const UiTextStyle& st, int align = UI_LEFT);
// Outlined text in one colour (the everyday label)
void ui_label(int x, int y, const char* s, uint16_t c, int scale, int align = UI_LEFT);

// Chamfered translucent panel with a border; accent != 0 draws an accent
// rule along the top edge.
void ui_panel(int x, int y, int w, int h, uint16_t accent, int alpha = 22);
// Darken towards UI_INK (fast: shifts and masks, two pixels per word). alpha
// picks the level: <= 16 keeps 1/2 of the background, <= 21 keeps 3/8,
// more keeps 1/4.
void ui_shadeRect(int x, int y, int w, int h, int alpha = 20);
void ui_shadeEllipse(int cx, int cy, int r, int alpha = 20);
// Animated selection bar (gradient, sweeping shine, edge marker)
void ui_selBar(int x, int y, int w, int h, float t, uint16_t c = UI_ACCENT);
// Small icons
void ui_carIcon(int x, int y, uint16_t paint);   // 18 x 9 side view
void ui_arrow(int cx, int cy, int size, int dir, uint16_t c);   // dir 0 up 1 down 2 left 3 right
// Controls hint line for this board (bottom of the menus)
const char* ui_hintNav();                          // e.g. "STICK SELECT  B OK  SELECT BACK"
const char* ui_hintConfirm();                      // e.g. "PRESS B"
// Darken the whole frame (pause, results): a = 0..32
void ui_dim(int a);
// Fade TO black over [0, dur) (screen exits)
void ui_fadeOut(float t, float dur);

// Title with the featured circuit (0..2, its name highlighted; the backdrop
// is that circuit's theme) and the time since it was featured (dips).
void ui_drawTitleEx(float t, int featured, float featuredT);
// Main menu: GRAND PRIX / QUICK RACE / DIFFICULTY / BACK, a line of help
// for the selected item, and the save statistics.
void ui_drawMainMenu(int sel, int difficulty, float t);
// Results with record flags (bit 0 new lap record, bit 1 new race record)
void ui_drawResultsEx(const ResultRow* rows, const int* points, int recordFlags, float t);
// Standings with the points of the last race and the previous order
// (prevOrder may be nullptr). nextCircuit < 0: final standings.
void ui_drawStandingsEx(const int* order, const int* points, const int* racePts,
                        const int* prevOrder, int raceNo, int races, int nextCircuit, float t);
// Champion: order = final standings (racer indices), points per racer.
// Draws the backdrop too (winner's car on a podium, confetti).
void ui_drawChampionEx(const int* order, const int* points, float t);
// Finish: place, race time, record flags
void ui_drawFinishEx(float t, int place, float raceTimeSec, int recordFlags);
// Lap banner with a second line (e.g. "LAST 0:34.21") and an optional tag
void ui_drawBannerEx(const char* text, const char* sub, const char* tag, uint16_t color, float t);
// Pause with the race state underneath the items; gp: quitting ends the GP
void ui_drawPauseEx(int sel, bool gp, float t);
// Circuit select for the carousel layout (the game draws the flyover)
void ui_drawCircuitSelectEx(int sel, float t, float selT);
// Grid + countdown header (GET READY / race number / circuit) and lights
void ui_drawCountdownEx(float t, bool gp, int gpRace, int gpRaces);
// Lights + 3-2-1 only (the demo)
void ui_drawCountdownLights(float t);

// ---- additions (integration) ----
// Boot: logs where the HOT shading span landed (IRAM check, ESP32 only).
void ui_init();
// Start-light state of the countdown t seconds after it began (the world
// gantry in render_road mirrors it): bit i set = light i of 5 lit red;
// *green = all five green (from GO until UI_GO_GREEN seconds after it).
int  ui_countdownLightMask(float t, bool* green);
#define UI_GO_GREEN 2.5f

#endif // UI_H
