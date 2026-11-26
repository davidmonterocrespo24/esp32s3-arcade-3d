/*
  ═══════════════════════════════════════════════════════════════
  GAME FLOW — implementation

  States (game.h) and what moves between them:
    TITLE      features the three circuits in turn (theme + backdrop);
               any button -> MENU, 10 s idle -> DEMO
    DEMO       countdown + the autopilot racing on the featured
               circuit; any button / 45 s / finish -> TITLE
    MENU       GRAND PRIX, QUICK RACE, DIFFICULTY, BACK
    SELECT     carousel of the circuits + BACK over a live flyover
    COUNTDOWN  gantry lights, 3-2-1, GO (cars held on the grid)
    RACE       START -> PAUSE (RESUME / RESTART / QUIT)
    FINISH     banner while the autopilot drives a 4 s cool-down
    RESULTS    -> GP: STANDINGS -> next race ... -> CHAMPION -> MENU
               -> QUICK: SELECT
  Screen changes that swap the scene fade out (0.2 s) and fade in
  (0.3 s); in-race overlays (pause, finish, results, standings) do not.

  Menus: stick Y / arrows move with auto-repeat, steer changes values
  (difficulty) and cycles the circuit carousel. On the two-button dev
  board a button moves on RELEASE (pressing both is confirm), so every
  screen works with steer + confirm only. Every menu has BACK.

  Save data is written (NVS on the ESP32) only on screens without a
  race running at full speed: results, champion, leaving the menu.
  ═══════════════════════════════════════════════════════════════
*/

#include <Arduino.h>
#include "game.h"
#include "config.h"
#include "board.h"
#include "platform.h"
#include "gfx.h"
#include "display.h"
#include "input.h"
#include "colors.h"
#include "utils.h"
#include "track.h"
#include "circuits.h"
#include "rendering.h"
#include "render_road.h"
#include "render_player.h"
#include "render_cars.h"
#include "render_hud.h"
#include "physics.h"
#include "race.h"
#include "rivals.h"
#include "ui.h"
#include "save.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

GameDebug gameDebug = { false, false, 0.0f, -1.0f, false, false };

// ═══════════════════════════════════════════════════════════════
//  TUNABLES
// ═══════════════════════════════════════════════════════════════
static const int   POINTS[NUM_RACERS] = { 10, 7, 5, 3, 2, 1 };
static const float TITLE_IDLE_DEMO = 10.0f;   // title -> demo after this long without input
static const float MENU_IDLE_TITLE = 60.0f;   // menus -> title (attract) after this long
static const float DEMO_TIME       = 45.0f;
static const float FINISH_TIME     = 4.0f;    // cool-down before the results
static const float FADE_IN         = 0.30f;
static const float FADE_OUT        = 0.20f;
static const float MENU_CYCLE      = 1.25f;   // sunset look behind the menus / champion
static const float CONFIRM_DELAY   = 0.8f;    // results / standings accept confirm after this
static const float REPEAT_DELAY    = 0.40f;   // menu auto-repeat: first repeat
static const float REPEAT_EVERY    = 0.12f;   //                   then every
static const float FLY_SPEED       = 3600.0f; // circuit-select flyover (world units / s)
// Day-cycle look of each circuit on the title (dusk city, coast sunset, alpine dawn)
static const float TITLE_CYCLES[NUM_CIRCUITS] = { 1.62f, 1.12f, 3.45f };

// ═══════════════════════════════════════════════════════════════
//  STATE
// ═══════════════════════════════════════════════════════════════
static GameState sState = GS_TITLE;
static float     sStateT = 0;
static bool      sFade = true;
typedef void (*Action)();
static Action    sPending = nullptr;          // runs when the fade-out ends
static float     sOutT = 0;
static InputState sIn, sPrev;

static int   sMenuSel = 0, sSelSel = 0, sPauseSel = 0;
static float sSelT = 0;                       // time since the carousel moved
static bool  sGP = false;
static int   sGPRace = 0;
static int   sGPPoints[NUM_RACERS];
static int   sRacePts[NUM_RACERS];
static int   sStandOrder[NUM_RACERS], sPrevOrder[NUM_RACERS];
static ResultRow sResults[NUM_RACERS];
static int   sRecFlags = 0;
static float sRecLapAtStart = 0;             // saved lap record when the race started
static bool  sSaveDirty = false;
static float sIdleT = 0;
static int   sFeatured = 0;                   // title: featured circuit
static float sFeatT = 0;
static float sGoT = -1;                       // seconds since GO (GO! overlay)
static int   sLastLap = 0;
static float sFlyPos = 0, sFlySpeed = 0;      // circuit-select flyover camera
static unsigned long sLastMs = 0;

// race banner (LAP 2/3, FINAL LAP, records)
static char        sBanText[24], sBanSub[32];
static const char* sBanTag = nullptr;
static uint16_t    sBanCol = COL_WHITE;
static float       sBanT = -1;

// Pause: the frozen race frame is kept in PSRAM and copied back each
// frame instead of rendering the scene again (~1 ms instead of a race frame)
static uint16_t* sSnap = nullptr;
static bool      sSnapValid = false;
static float     sFrozenTime = 0;

// Frame statistics for the HUD and the status line
static float sFps = 0, sLcdFps = 0, sFrameMs = 0;
static uint32_t sLastShown = 0;
static unsigned long sLastStatMs = 0, sLastStatusMs = 0;
static int sFramesSinceStat = 0;

static const char* const STATE_NAMES[GS_COUNT] = {
  "TITLE", "DEMO", "MENU", "SELECT", "COUNTDOWN", "RACE", "PAUSE",
  "FINISH", "RESULTS", "STANDINGS", "CHAMPION" };

GameState game_state() { return sState; }
const char* game_stateName(GameState s) { return s < GS_COUNT ? STATE_NAMES[s] : "?"; }
float game_stateTime() { return sStateT; }
bool game_isGP() { return sGP; }
int game_gpRace() { return sGPRace; }
int game_gpPoints(int racer) { return (racer >= 0 && racer < NUM_RACERS) ? sGPPoints[racer] : 0; }
int game_recordFlags() { return sRecFlags; }
bool game_inTransition() { return sPending != nullptr; }

// ═══════════════════════════════════════════════════════════════
//  INPUT: edges, activity, menu navigation with auto-repeat
// ═══════════════════════════════════════════════════════════════
static inline bool pressed(bool now, bool prev) { return now && !prev; }
static bool confirmPressed() { return pressed(sIn.confirm, sPrev.confirm); }
static bool backPressed()    { return pressed(sIn.back, sPrev.back); }
static bool startPressed()   { return pressed(sIn.start, sPrev.start); }

// A deliberate press of anything (stick drift inside 0.6 does not count)
static bool anyPressed() {
  return confirmPressed() || startPressed() || backPressed() || pressed(sIn.brake, sPrev.brake) ||
         pressed(sIn.turbo, sPrev.turbo) ||
         (fabsf(sIn.steer) > 0.6f && fabsf(sPrev.steer) <= 0.6f) ||
         (fabsf(sIn.stickY) > 0.6f && fabsf(sPrev.stickY) <= 0.6f);
}
static bool anyHeld() {
  return sIn.confirm || sIn.start || sIn.back || sIn.brake || sIn.turbo ||
         fabsf(sIn.steer) > 0.6f || fabsf(sIn.stickY) > 0.6f;
}

struct Repeat { bool held; float t; };
static Repeat sRepX = { false, 0 }, sRepY = { false, 0 };

// -1 / +1 on the press, then auto-repeat while held
static int axisRepeat(float v, Repeat& r, float dt) {
  int d = v < -0.5f ? -1 : (v > 0.5f ? 1 : 0);
  if (!d) { r.held = false; r.t = 0; return 0; }
  if (!r.held) { r.held = true; r.t = 0; return d; }
  r.t += dt;
  if (r.t >= REPEAT_DELAY) { r.t -= REPEAT_EVERY; return d; }
  return 0;
}

struct Nav { int y, x; };   // events this frame: y moves the selection, x changes values
static Nav navRead(float dt) {
  Nav n = { 0, 0 };
  if (sPending) return n;
#if PLATFORM_ESP32 && BOARD == BOARD_DEVKIT
  const bool devkit = true;
#else
  const bool devkit = gameDebug.devkitNav;    // the harness can test the dev-board scheme
#endif
  if (devkit) {
    // Two buttons: one button moves on RELEASE unless the other one joined
    // (both = confirm), so a confirm chord never moves the selection.
    static int held = 0;
    static bool chord = false;
    int s = sIn.steer > 0.5f ? 1 : (sIn.steer < -0.5f ? -1 : 0);
    if (sIn.confirm) { chord = true; held = 0; }
    else if (s != 0) { if (!chord) held = s; }
    else { if (held != 0 && !chord) n.y = held; held = 0; chord = false; }
  } else {
    // Dominant axis only, so a diagonal push does not move and change at once
    float ay = fabsf(sIn.stickY), ax = fabsf(sIn.steer);
    n.y = axisRepeat(ay >= ax ? sIn.stickY : 0.0f, sRepY, dt);
    n.x = axisRepeat(ax > ay ? sIn.steer : 0.0f, sRepX, dt);
  }
  return n;
}

// ═══════════════════════════════════════════════════════════════
//  STATE HELPERS
// ═══════════════════════════════════════════════════════════════
static void enter(GameState s, bool fade = true) {
  sState = s;
  sStateT = 0;
  sFade = fade;
  sIdleT = 0;
  sRepX.held = sRepY.held = true;     // a key still held from the last screen does not move the new one
}

// Start a fade-out; `a` runs when the screen is black
static void go(Action a) {
  if (sPending) return;
  sPending = a;
  sOutT = 0;
}

static void setCycle(float c) {
  applyDayCycle(gameDebug.freezeCycle ? gameDebug.cycle : c);
}

static void raceCycle() {
  const CircuitInfo& c = curCircuit();
  float f = clampF(racers[PLAYER_ID].progress / fmaxF(race_distance(), 1.0f), 0, 1);
  setCycle(lerpF(c.cycleStart, c.cycleEnd, f));
}

static void commitIfDirty() {
  if (sSaveDirty) { save_commit(); sSaveDirty = false; }
}

// Title backdrop: theme, background and light of a circuit, without
// building its track (nothing of the track is shown on the title)
static void featureCircuit(int c) {
  sFeatured = iclamp(c, 0, NUM_CIRCUITS - 1);
  sFeatT = 0;
  const CircuitInfo& ci = CIRCUITS[sFeatured];
  setTheme(ci.theme);
  initBackground(ci.theme);
  setDayCycle(gameDebug.freezeCycle ? gameDebug.cycle : TITLE_CYCLES[sFeatured]);
}

static void goTitle() {
  autopilot = false;
  sGP = false;
  featureCircuit(sFeatured);
  enter(GS_TITLE);
}

static void enterMenu() {
  autopilot = false;
  setCycle(MENU_CYCLE);
  enter(GS_MENU);
}

// Circuit select: build the circuit, put the cars on its grid and start
// the flyover behind the grid
static void flyReset() {
  float back = GRID_FRONT + NUM_RACERS * GRID_ROW * 0.5f + 1400.0f;
  sFlyPos = loopIncrease(trackLength - back, 0, trackLength);
  sFlySpeed = FLY_SPEED * 0.35f;
  skyOffset = 0;
}

static void selectCircuit(int c) {
  sSelSel = c;
  sSelT = 0;
  if (c >= 0 && c < NUM_CIRCUITS) {
    buildCircuit(c);
    race_init();
    setDayCycle(gameDebug.freezeCycle ? gameDebug.cycle : CIRCUITS[c].cycleStart);
    flyReset();
  }
}

static void enterSelect() {
  autopilot = false;
  selectCircuit(iclamp(sSelSel, 0, NUM_CIRCUITS - 1));
  enter(GS_SELECT);
}

static void beginRace() {
  race_go();
  if (gameDebug.teleportZ >= 0) position = loopIncrease(gameDebug.teleportZ, 0, trackLength);
  sGoT = 0;
}

static void startRace(int circuit) {
  buildCircuit(circuit);
  race_init();
  hud_reset();
  autopilot = gameDebug.autoDrive;
  skyOffset = 0;
  sLastLap = 0;
  sBanT = -1;
  sGoT = -1;
  sRecFlags = 0;
  sRecLapAtStart = saveData.bestLap[iclamp(currentCircuit, 0, NUM_CIRCUITS - 1)];
  raceCycle();
  if (gameDebug.noCountdown) { beginRace(); enter(GS_RACE); }
  else enter(GS_COUNTDOWN);
}

static void startGP() {
  sGP = true;
  sGPRace = 0;
  for (int i = 0; i < NUM_RACERS; i++) { sGPPoints[i] = 0; sStandOrder[i] = i; sPrevOrder[i] = i; }
  startRace(0);
}

static void startDemo() {
  sGP = false;
  buildCircuit(sFeatured);
  race_init();
  hud_reset();
  autopilot = true;
  skyOffset = 0;
  sGoT = -1;
  raceCycle();
  enter(GS_DEMO);
}

// Actions run at the end of a fade-out
static void actMenu()       { commitIfDirty(); enterMenu(); }
static void actTitle()      { commitIfDirty(); goTitle(); }
static void actSelect()     { commitIfDirty(); enterSelect(); }
static void actGP()         { commitIfDirty(); startGP(); }
static void actQuickRace()  { commitIfDirty(); sGP = false; startRace(sSelSel); }
static void actRestart()    { startRace(currentCircuit); }
static void actDemo()       { startDemo(); }
static void actNextGPRace() { sGPRace++; startRace(sGPRace); }
static void actChampion()   { autopilot = false; setCycle(MENU_CYCLE); enter(GS_CHAMPION); }
static void actQuit()       { sGP = false; enterMenu(); }

static void banner(const char* text, const char* sub, const char* tag, uint16_t col) {
  snprintf(sBanText, sizeof(sBanText), "%s", text);
  if (sub) snprintf(sBanSub, sizeof(sBanSub), "%s", sub);
  else sBanSub[0] = 0;
  sBanTag = tag;
  sBanCol = col;
  sBanT = 0;
}

// Physics + rivals + race for one frame (also during the cool-down)
static void updateDriving(float dt) {
  if (!crashed) {
    handleInput(dt, sIn);
    updatePhysics(dt);
    checkCollisions(dt);
  } else {
    updateCrash(dt);
  }
  rivals_update(dt);
  race_update(dt);
  // Parallax: the background turns with the curves
  int pSeg = findSegIdx(position + playerZdist);
  skyOffset += segAt(pSeg).curve * (speed / maxSpeed) * 150.0f * dt;
}

// Racer indices by GP points (ties: better place in the last race)
static void standingsOrder(int* order) {
  int placeOf[NUM_RACERS];
  for (int p = 0; p < NUM_RACERS; p++) placeOf[sResults[p].racer] = p;
  for (int i = 0; i < NUM_RACERS; i++) order[i] = i;
  for (int i = 1; i < NUM_RACERS; i++) {
    int k = order[i], j = i - 1;
    while (j >= 0 && (sGPPoints[k] > sGPPoints[order[j]] ||
                      (sGPPoints[k] == sGPPoints[order[j]] && placeOf[k] < placeOf[order[j]]))) {
      order[j + 1] = order[j];
      j--;
    }
    order[j + 1] = k;
  }
}

// When the player crosses the line: records (RAM only, written at RESULTS)
static void finishRecords() {
  sRecFlags = 0;
  const Racer& P = racers[PLAYER_ID];
  if (P.bestLap > 0 && save_recordLap(currentCircuit, P.bestLap)) sRecFlags |= 1;
  if (P.finished && save_recordRace(currentCircuit, P.finishTime)) sRecFlags |= 2;
}

static void computeResults() {
  race_results(sResults);
  for (int i = 0; i < NUM_RACERS; i++) sRacePts[i] = 0;
  for (int p = 0; p < NUM_RACERS; p++) sRacePts[sResults[p].racer] = POINTS[p];
  if (sGP) {
    for (int i = 0; i < NUM_RACERS; i++) { sPrevOrder[i] = sStandOrder[i]; sGPPoints[i] += sRacePts[i]; }
    standingsOrder(sStandOrder);
  }
  if (saveData.races < 65535) saveData.races++;
  saveData.difficulty = (uint8_t)difficulty;
  if (!sGP) saveData.lastCircuit = (uint8_t)currentCircuit;
  save_commit();   // the race is over: a flash write is fine now
  sSaveDirty = false;
}

// ═══════════════════════════════════════════════════════════════
//  UPDATE
// ═══════════════════════════════════════════════════════════════
static void updateRaceBanners() {
  const Racer& P = racers[PLAYER_ID];
  const int laps = curCircuit().laps;
  if (P.lap == sLastLap) return;
  sLastLap = P.lap;
  if (P.lap <= 0 || P.finished) return;
  char sub[32], tb[16];
  ui_fmtTime(tb, sizeof(tb), P.lastLap);
  snprintf(sub, sizeof(sub), "LAST LAP %s", tb);
  const bool record = sRecLapAtStart > 0 && P.lastLap > 0 && P.lastLap < sRecLapAtStart &&
                      P.lastLap <= P.bestLap + 1e-4f;
  if (P.lap == laps - 1) banner("FINAL LAP", sub, record ? "NEW LAP RECORD" : nullptr, UI_GOLD);
  else {
    char t[24];
    snprintf(t, sizeof(t), "LAP %d/%d", iclamp(P.lap + 1, 0, 99), iclamp(laps, 0, 99));
    banner(t, sub, record ? "NEW LAP RECORD" : nullptr, UI_ACCENT2);
  }
}

static void update(float dt) {
  sStateT += dt;
  if (sState != GS_PAUSE) {            // pause freezes the race overlays too
    if (sGoT >= 0) sGoT += dt;
    if (sBanT >= 0) sBanT += dt;
  }
  if (sPending) {
    sOutT += dt;
    sPrev = sIn;                       // no edges while fading out
  }
  if (anyHeld()) sIdleT = 0; else sIdleT += dt;

  switch (sState) {
    case GS_TITLE:
      sFeatT += dt;
      if (sFeatT >= UI_TITLE_FEATURE_TIME && !sPending) featureCircuit((sFeatured + 1) % NUM_CIRCUITS);
      if (sStateT > 0.5f && anyPressed()) { sMenuSel = 0; go(actMenu); }
      else if (sIdleT > TITLE_IDLE_DEMO) go(actDemo);
      break;

    case GS_DEMO:
      if (sStateT < UI_COUNTDOWN_GO) {
        rivals_update(dt);
        race_update(dt);
      } else {
        if (!raceRunning) { race_go(); sGoT = 0; }
        updateDriving(dt);
      }
      raceCycle();
      if (!sPending &&
          ((sStateT > 0.5f && anyPressed()) || sStateT > DEMO_TIME || racers[PLAYER_ID].finished)) {
        sFeatured = (sFeatured + 1) % NUM_CIRCUITS;   // the title then features the next circuit
        go(actTitle);
      }
      break;

    case GS_MENU: {
      Nav n = navRead(dt);
      if (n.y) sMenuSel = (sMenuSel + n.y + 4) % 4;
      if (n.x && sMenuSel == 2) {
        difficulty = (difficulty + n.x + 3) % 3;
        saveData.difficulty = (uint8_t)difficulty;
        sSaveDirty = true;
      }
      if (sPending) break;
      if (backPressed()) go(actTitle);
      else if (confirmPressed()) {
        switch (sMenuSel) {
          case 0: go(actGP); break;
          case 1: sSelSel = iclamp(saveData.lastCircuit, 0, NUM_CIRCUITS - 1); go(actSelect); break;
          case 2:
            difficulty = (difficulty + 1) % 3;
            saveData.difficulty = (uint8_t)difficulty;
            sSaveDirty = true;
            break;
          default: go(actTitle); break;
        }
      } else if (sIdleT > MENU_IDLE_TITLE) go(actTitle);
      break;
    }

    case GS_SELECT: {
      sSelT += dt;
      // flyover: ease in, then cruise along the lap
      sFlySpeed += (FLY_SPEED - sFlySpeed) * clampF(dt * 0.8f, 0, 1);
      sFlyPos = loopIncrease(sFlyPos, sFlySpeed * dt, trackLength);
      skyOffset += segAt(findSegIdx(sFlyPos + playerZdist)).curve * (sFlySpeed / maxSpeed) * 150.0f * dt;
      Nav n = navRead(dt);
      int d = n.x + n.y;
      if (d) {
        d = d > 0 ? 1 : -1;
        selectCircuit((sSelSel + d + NUM_CIRCUITS + 1) % (NUM_CIRCUITS + 1));
      }
      if (sPending) break;
      if (backPressed()) go(actMenu);
      else if (confirmPressed()) {
        if (sSelSel >= NUM_CIRCUITS) go(actMenu);
        else go(actQuickRace);
      } else if (sIdleT > MENU_IDLE_TITLE) go(actTitle);
      break;
    }

    case GS_COUNTDOWN:
      rivals_update(dt);
      race_update(dt);
      raceCycle();
      if (sStateT >= UI_COUNTDOWN_GO) { beginRace(); enter(GS_RACE, false); }
      break;

    case GS_RACE: {
      if (sStateT > 0.3f && startPressed() && !sPending) {
        sPauseSel = 0;
        sSnapValid = false;
        enter(GS_PAUSE, false);
        break;
      }
      updateDriving(dt);
      raceCycle();
      updateRaceBanners();
      if (racers[PLAYER_ID].finished) {
        autopilot = true;
        finishRecords();
        enter(GS_FINISH, false);
      }
      break;
    }

    case GS_PAUSE: {
      Nav n = navRead(dt);
      if (n.y) sPauseSel = (sPauseSel + n.y + 3) % 3;
      if (sPending) break;
      if (backPressed() || (startPressed() && !sIn.confirm)) enter(GS_RACE, false);
      else if (confirmPressed()) {
        if (sPauseSel == 0) enter(GS_RACE, false);
        else if (sPauseSel == 1) go(actRestart);
        else go(actQuit);
      }
      break;
    }

    case GS_FINISH:
      updateDriving(dt);
      raceCycle();
      if (sStateT >= FINISH_TIME) { computeResults(); enter(GS_RESULTS, false); }
      break;

    case GS_RESULTS:
      updateDriving(dt);
      raceCycle();
      if (sStateT > CONFIRM_DELAY && confirmPressed() && !sPending) {
        if (sGP) enter(GS_STANDINGS, false);
        else { sSelSel = currentCircuit; go(actSelect); }
      }
      break;

    case GS_STANDINGS:
      updateDriving(dt);
      raceCycle();
      if (sStateT > CONFIRM_DELAY && confirmPressed() && !sPending) {
        if (sGPRace < NUM_CIRCUITS - 1) go(actNextGPRace);
        else {
          if (sStandOrder[0] == PLAYER_ID) { save_recordGPWin(difficulty); save_commit(); }
          go(actChampion);
        }
      }
      break;

    case GS_CHAMPION:
      if (sStateT > 1.5f && confirmPressed() && !sPending) go(actQuit);
      break;

    default:
      goTitle();
      break;
  }

  if (sPending && sOutT >= FADE_OUT) {
    Action a = sPending;
    sPending = nullptr;
    a();
  }
}

// ═══════════════════════════════════════════════════════════════
//  RENDER
// ═══════════════════════════════════════════════════════════════
static void renderRaceScene(float timeSec) {
  drawSky(skyOffset, timeSec);
  drawRoad(position, playerX, playerZdist, cameraDepth);
  drawPlayerCar(timeSec);
  cars_drawNear();
}

static void renderFlyover(float timeSec) {
  // the cars on the grid are drawn relative to a phantom player car
  racers[PLAYER_ID].z = loopIncrease(sFlyPos + playerZdist, 0, trackLength);
  drawSky(skyOffset, timeSec);
  drawRoad(sFlyPos, 0.0f, playerZdist, cameraDepth);
  cars_drawNear();
  // a short dip when the carousel switches circuits
  if (sSelT < 0.25f) ui_dim((int)((1.0f - sSelT / 0.25f) * 32.0f));
}

static void renderPause(float timeSec) {
  const size_t bytes = (size_t)SCR_W * SCR_H * sizeof(uint16_t);
  if (sSnap && sSnapValid) {
    memcpy(gfx_fb, sSnap, bytes);
  } else {
    renderRaceScene(sFrozenTime);
    drawHUD(sFrozenTime, sFps, sLcdFps, sFrameMs);
    ui_dim(14);
    if (sSnap) { memcpy(sSnap, gfx_fb, bytes); sSnapValid = true; }
  }
  ui_drawPauseEx(sPauseSel, sGP, timeSec);
}

static void render(float timeSec) {
  switch (sState) {
    case GS_TITLE:
      ui_drawTitleEx(timeSec, sFeatured, sFeatT);
      break;
    case GS_MENU:
      ui_drawMenuBackdrop(timeSec);
      ui_drawMainMenu(sMenuSel, difficulty, timeSec);
      break;
    case GS_SELECT:
      renderFlyover(timeSec);
      ui_drawCircuitSelectEx(sSelSel, timeSec, sSelT);
      break;
    case GS_DEMO:
      renderRaceScene(timeSec);
      drawHUD(timeSec, sFps, sLcdFps, sFrameMs);
      if (sStateT < UI_COUNTDOWN_GO) ui_drawCountdownLights(sStateT);
      ui_drawGo(sGoT);
      ui_drawDemoBanner(sStateT);
      break;
    case GS_COUNTDOWN:
      renderRaceScene(timeSec);
      drawHUD(timeSec, sFps, sLcdFps, sFrameMs);
      ui_drawCountdownEx(sStateT, sGP, sGPRace, NUM_CIRCUITS);
      break;
    case GS_RACE:
      renderRaceScene(timeSec);
      drawHUD(timeSec, sFps, sLcdFps, sFrameMs);
      if (crashed) ui_drawCrash(crashTime);
      ui_drawGo(sGoT);
      if (sBanT >= 0) ui_drawBannerEx(sBanText, sBanSub[0] ? sBanSub : nullptr, sBanTag, sBanCol, sBanT);
      sFrozenTime = timeSec;
      break;
    case GS_PAUSE:
      renderPause(timeSec);
      break;
    case GS_FINISH:
      renderRaceScene(timeSec);
      drawHUD(timeSec, sFps, sLcdFps, sFrameMs);
      ui_drawFinishEx(sStateT, racers[PLAYER_ID].place, racers[PLAYER_ID].finishTime, sRecFlags);
      break;
    case GS_RESULTS:
      renderRaceScene(timeSec);
      ui_drawResultsEx(sResults, sGP ? sRacePts : nullptr, sRecFlags, sStateT);
      break;
    case GS_STANDINGS:
      renderRaceScene(timeSec);
      ui_drawStandingsEx(sStandOrder, sGPPoints, sRacePts, sGPRace > 0 ? sPrevOrder : nullptr, sGPRace + 1,
                         NUM_CIRCUITS, sGPRace + 1 < NUM_CIRCUITS ? sGPRace + 1 : -1, sStateT);
      break;
    case GS_CHAMPION:
      ui_drawChampionEx(sStandOrder, sGPPoints, sStateT);
      break;
    default: break;
  }
  if (sPending) ui_fadeOut(sOutT, FADE_OUT);
  else if (sFade) ui_fade(sStateT, FADE_IN);
}

// ═══════════════════════════════════════════════════════════════
//  SETUP / LOOP
// ═══════════════════════════════════════════════════════════════
static void updateStats(unsigned long now, float frameMs) {
  sFramesSinceStat++;
  sFrameMs += (frameMs - sFrameMs) * 0.1f;
  if (now - sLastStatMs >= 500) {
    float secs = (now - sLastStatMs) / 1000.0f;
    sFps = sFramesSinceStat / secs;
    uint32_t shown = displayStats.shown;
    sLcdFps = (shown - sLastShown) / secs;
    sLastShown = shown;
    sFramesSinceStat = 0;
    sLastStatMs = now;
  }
  if (now - sLastStatusMs >= 1000) {
    sLastStatusMs = now;
    plat_status(sFps, sFrameMs, sLcdFps);
  }
}

void game_setup() {
#if defined(ARDUINO_ARCH_ESP32) && !ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxBufferSize(1024);   // UART: buffered, so a log line never waits for the wire
#endif
  Serial.begin(115200);
#if defined(ARDUINO_ARCH_ESP32) && ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);       // USB CDC: never block when no host reads the port
#endif
  delay(50);
  plat_init();
  plat_report_memory("boot");

  input_init();
  // DMA bands + PSRAM snapshots first, then the big internal framebuffer
  // while the internal heap is still unfragmented.
  if (!display_init()) plat_log("display_init FAILED\n");
  if (!gfx_init()) plat_log("gfx_init FAILED: no memory for the framebuffer\n");
  initCarRenderer();
  if (!track_init()) plat_log("track_init FAILED\n");
  // Frozen frame for the pause screen: PSRAM only (never worth internal RAM)
  sSnap = (uint16_t*)plat_alloc_psram((size_t)SCR_W * SCR_H * sizeof(uint16_t));
  plat_log("ui: pause snapshot %s\n", sSnap ? "153600 B in PSRAM" : "unavailable (pause re-renders)");
  plat_report_memory("ready");

  save_init();
  difficulty = iclamp(saveData.difficulty, 0, 2);
  sSelSel = iclamp(saveData.lastCircuit, 0, NUM_CIRCUITS - 1);
  initPhysics();
  buildCircuit(0);
  race_init();
  goTitle();

  sLastMs = millis();
  sLastStatMs = sLastMs;
  sLastStatusMs = sLastMs;
}

void game_loop() {
  unsigned long now = millis();
  unsigned long t0us = micros();
  float dt = (now - sLastMs) / 1000.0f;
  sLastMs = now;
  if (dt > 0.1f) dt = 0.1f;
  if (dt < 0.0f) dt = 0.0f;
  float timeSec = now / 1000.0f;

  sPrev = sIn;
  sIn = input_read();

  update(dt);
  render(timeSec);

  // Hand the frame to the scanout. On the ESP32 this copies it to a PSRAM
  // snapshot (or drops it if the panel is still busy) and returns at once.
  display_present(gfx_fb);
  updateStats(now, (micros() - t0us) / 1000.0f);
}

// ═══════════════════════════════════════════════════════════════
//  HARNESS HOOKS
// ═══════════════════════════════════════════════════════════════
void game_debugStartRace(int circuit) {
  sGP = false;
  startRace(iclamp(circuit, 0, NUM_CIRCUITS - 1));
}

void game_debugStartGP() {
  startGP();
}

// Drive the current race to the player's finish with the autopilot (no
// rendering); with `classify`, also the cool-down and the classification
static void ffRace(bool classify = true) {
  const float dt = 1.0f / 30.0f;
  InputState none = { 0, 0, false, false, false, false, false, false };
  sIn = sPrev = none;
  autopilot = true;
  if (!raceRunning) beginRace();
  for (int i = 0; i < 30 * 900 && !racers[PLAYER_ID].finished; i++) {
    updateDriving(dt);
    raceCycle();
    updateRaceBanners();
  }
  finishRecords();
  if (!classify) return;
  for (int i = 0; i < (int)(FINISH_TIME * 30); i++) { updateDriving(dt); raceCycle(); }
  computeResults();
}

bool game_debugJump(const char* name, int circuit) {
  int s = -1;
  for (int i = 0; i < GS_COUNT; i++)
    if (name && strcmp(name, STATE_NAMES[i]) == 0) s = i;
  if (s < 0) return false;
  circuit = iclamp(circuit, 0, NUM_CIRCUITS - 1);
  sPending = nullptr;
  switch ((GameState)s) {
    case GS_TITLE:     sFeatured = circuit; goTitle(); break;
    case GS_DEMO:      sFeatured = circuit; startDemo(); break;
    case GS_MENU:      enterMenu(); break;
    case GS_SELECT:    sSelSel = circuit; enterSelect(); break;
    case GS_COUNTDOWN: { bool nc = gameDebug.noCountdown; gameDebug.noCountdown = false;
                         sGP = false; startRace(circuit); gameDebug.noCountdown = nc; break; }
    case GS_RACE:      { bool nc = gameDebug.noCountdown; gameDebug.noCountdown = true;
                         sGP = false; startRace(circuit); gameDebug.noCountdown = nc; break; }
    case GS_PAUSE:     { bool nc = gameDebug.noCountdown; gameDebug.noCountdown = true;
                         sGP = false; startRace(circuit); gameDebug.noCountdown = nc;
                         sPauseSel = 0; sSnapValid = false; sFrozenTime = millis() / 1000.0f;
                         enter(GS_PAUSE, false); break; }
    case GS_FINISH:    sGP = false; startRace(circuit); ffRace(false);
                       sGoT = -1; enter(GS_FINISH, false);
                       break;
    case GS_RESULTS:   sGP = false; startRace(circuit); ffRace();
                       enter(GS_RESULTS, false);
                       break;
    case GS_STANDINGS: startGP();
                       for (;;) {
                         ffRace();
                         if (sGPRace >= circuit) break;
                         sGPRace++;
                         startRace(sGPRace);
                       }
                       enter(GS_STANDINGS, false);
                       break;
    case GS_CHAMPION:  startGP();
                       for (;;) {
                         ffRace();
                         if (sGPRace >= NUM_CIRCUITS - 1) break;
                         sGPRace++;
                         startRace(sGPRace);
                       }
                       if (sStandOrder[0] == PLAYER_ID) save_recordGPWin(difficulty);
                       actChampion();
                       break;
    default: return false;
  }
  return true;
}
