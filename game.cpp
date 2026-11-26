/*
  ═══════════════════════════════════════════════════════════════
  GAME FLOW — implementation (basic version of every state)
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

GameDebug gameDebug = { false, false, 0.0f, -1.0f, false };

static const int   POINTS[NUM_RACERS] = { 10, 7, 5, 3, 2, 1 };
static const float TITLE_IDLE_DEMO = 10.0f;   // title -> demo after this long without input
static const float DEMO_TIME       = 45.0f;
static const float FINISH_TIME     = 4.0f;    // cool-down before the results
static const float FADE_TIME       = 0.3f;
static const float TITLE_CYCLE     = 1.25f;   // sunset look behind the title and menus

static GameState sState = GS_TITLE;
static float     sStateT = 0;
static bool      sFade = true;
static InputState sIn, sPrev;
static int   sMenuSel = 0, sSelSel = 0, sPauseSel = 0;
static bool  sGP = false;
static int   sGPRace = 0;
static int   sGPPoints[NUM_RACERS];
static int   sRacePts[NUM_RACERS];
static ResultRow sResults[NUM_RACERS];
static float sIdleT = 0;
static int   sDemoCircuit = 0;
static float sGoT = -1;                    // seconds since GO (banner)
static float sBannerT = -1;
static const char* sBanner = "";
static uint16_t sBannerCol = COL_WHITE;
static int   sLastLap = 0;
static unsigned long sLastMs = 0;

// Frame statistics for the HUD
static float sFps = 0, sLcdFps = 0, sFrameMs = 0;
static uint32_t sLastShown = 0;
static unsigned long sLastStatMs = 0;
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

// ═══════════════════════════════════════════════════════════════
//  INPUT EDGES AND MENU NAVIGATION
// ═══════════════════════════════════════════════════════════════
static inline bool pressed(bool now, bool prev) { return now && !prev; }
static bool confirmPressed() { return pressed(sIn.confirm, sPrev.confirm); }
static bool backPressed()    { return pressed(sIn.back, sPrev.back); }
static bool startPressed()   { return pressed(sIn.start, sPrev.start); }
static bool anyPressed()     { return sIn.any && !sPrev.any; }

// -1 previous / +1 next. Stick Y (arrows) acts on press with auto-repeat;
// steer left/right also moves, so the dev board (steer + confirm only) can
// use every menu. There, confirm is both buttons together, so steering acts
// on RELEASE and only if the other button never joined (a chord).
static int navDelta(float dt) {
  static float vHold = 0;
  int d = 0;
  int v = sIn.stickY < -0.5f ? -1 : (sIn.stickY > 0.5f ? 1 : 0);
  if (v == 0) {
    vHold = 0;
  } else {
    if (vHold == 0) d = v;
    vHold += dt;
    if (vHold > 0.45f) { d = v; vHold = 0.30f; }
  }
  int s = sIn.steer > 0.5f ? 1 : (sIn.steer < -0.5f ? -1 : 0);
#if BOARD == BOARD_DEVKIT
  static int held = 0;
  static bool chord = false;
  if (sIn.confirm) { chord = true; held = 0; }
  else if (s != 0) { if (!chord) held = s; }
  else { if (held != 0 && !chord) d = held; held = 0; chord = false; }
#else
  int ps = sPrev.steer > 0.5f ? 1 : (sPrev.steer < -0.5f ? -1 : 0);
  if (s != 0 && s != ps) d = s;
#endif
  return d;
}

// ═══════════════════════════════════════════════════════════════
//  STATE HELPERS
// ═══════════════════════════════════════════════════════════════
static void enter(GameState s, bool fade = true) {
  sState = s;
  sStateT = 0;
  sFade = fade;
}

static void setCycle(float c) {
  applyDayCycle(gameDebug.freezeCycle ? gameDebug.cycle : c);
}

static void raceCycle() {
  const CircuitInfo& c = curCircuit();
  float f = clampF(racers[PLAYER_ID].progress / fmaxF(race_distance(), 1.0f), 0, 1);
  setCycle(lerpF(c.cycleStart, c.cycleEnd, f));
}

static void goTitle() {
  autopilot = false;
  sIdleT = 0;
  setCycle(TITLE_CYCLE);
  enter(GS_TITLE);
}

static void beginRace() {
  race_go();
  if (gameDebug.teleportZ >= 0) position = loopIncrease(gameDebug.teleportZ, 0, trackLength);
  sGoT = 0;
}

static void startRace(int circuit) {
  buildCircuit(circuit);
  race_init();
  autopilot = gameDebug.autoDrive;
  skyOffset = 0;
  sLastLap = 0;
  sBannerT = -1;
  sGoT = -1;
  raceCycle();
  if (gameDebug.noCountdown) { beginRace(); enter(GS_RACE); }
  else enter(GS_COUNTDOWN);
}

static void startGP() {
  sGP = true;
  sGPRace = 0;
  for (int i = 0; i < NUM_RACERS; i++) sGPPoints[i] = 0;
  startRace(0);
}

static void startDemo() {
  buildCircuit(sDemoCircuit);
  sDemoCircuit = (sDemoCircuit + 1) % NUM_CIRCUITS;
  race_init();
  race_go();
  autopilot = true;
  skyOffset = 0;
  enter(GS_DEMO);
}

static void banner(const char* text, uint16_t col) {
  sBanner = text;
  sBannerCol = col;
  sBannerT = 0;
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

static void computeResults() {
  race_results(sResults);
  for (int i = 0; i < NUM_RACERS; i++) sRacePts[i] = 0;
  for (int p = 0; p < NUM_RACERS; p++) sRacePts[sResults[p].racer] = POINTS[p];
  if (sGP) for (int i = 0; i < NUM_RACERS; i++) sGPPoints[i] += sRacePts[i];
  const Racer& P = racers[PLAYER_ID];
  if (P.bestLap > 0) save_recordLap(currentCircuit, P.bestLap);
  if (P.finished) save_recordRace(currentCircuit, P.finishTime);
  if (saveData.races < 65535) saveData.races++;
  saveData.difficulty = (uint8_t)difficulty;
  if (!sGP) saveData.lastCircuit = (uint8_t)currentCircuit;
  save_commit();
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

// ═══════════════════════════════════════════════════════════════
//  UPDATE
// ═══════════════════════════════════════════════════════════════
static void update(float dt) {
  sStateT += dt;
  if (sGoT >= 0) sGoT += dt;
  if (sBannerT >= 0) sBannerT += dt;

  switch (sState) {
    case GS_TITLE:
      if (sIn.any) sIdleT = 0; else sIdleT += dt;
      // any control leaves the title (the dev board has no single START button)
      if (sStateT > 0.5f && (confirmPressed() || startPressed() || anyPressed())) { sMenuSel = 0; enter(GS_MENU); }
      else if (sIdleT > TITLE_IDLE_DEMO) startDemo();
      break;

    case GS_DEMO:
      updateDriving(dt);
      raceCycle();
      if ((sStateT > 0.5f && anyPressed()) || sStateT > DEMO_TIME || racers[PLAYER_ID].finished) goTitle();
      break;

    case GS_MENU: {
      sMenuSel = (sMenuSel + navDelta(dt) + 4) % 4;
      if (backPressed()) goTitle();
      else if (confirmPressed()) {
        switch (sMenuSel) {
          case 0: startGP(); break;
          case 1:
            sSelSel = iclamp(saveData.lastCircuit, 0, NUM_CIRCUITS - 1);
            buildCircuit(sSelSel);
            setCycle(TITLE_CYCLE);
            enter(GS_SELECT);
            break;
          case 2:
            difficulty = (difficulty + 1) % 3;
            saveData.difficulty = (uint8_t)difficulty;
            break;
          default: goTitle(); break;
        }
      }
      break;
    }

    case GS_SELECT: {
      int d = navDelta(dt);
      if (d) {
        sSelSel = (sSelSel + d + NUM_CIRCUITS + 1) % (NUM_CIRCUITS + 1);
        if (sSelSel < NUM_CIRCUITS) { buildCircuit(sSelSel); setCycle(TITLE_CYCLE); }
      }
      if (backPressed()) enter(GS_MENU);
      else if (confirmPressed()) {
        if (sSelSel >= NUM_CIRCUITS) enter(GS_MENU);
        else { sGP = false; startRace(sSelSel); }
      }
      break;
    }

    case GS_COUNTDOWN:
      rivals_update(dt);
      race_update(dt);
      raceCycle();
      if (sStateT >= UI_COUNTDOWN_GO) { beginRace(); enter(GS_RACE, false); }
      break;

    case GS_RACE: {
      if (sStateT > 0.3f && startPressed()) { sPauseSel = 0; enter(GS_PAUSE, false); break; }
      updateDriving(dt);
      raceCycle();
      const Racer& P = racers[PLAYER_ID];
      const int laps = curCircuit().laps;
      if (P.lap != sLastLap) {
        static char lapText[32];
        sLastLap = P.lap;
        if (P.lap == laps - 1) banner("FINAL LAP", COL_YELLOW);
        else if (P.lap < laps) {
          snprintf(lapText, sizeof(lapText), "LAP %d/%d", iclamp(P.lap + 1, 0, 99), iclamp(laps, 0, 99));
          banner(lapText, COL_WHITE);
        }
      }
      if (P.finished) { autopilot = true; enter(GS_FINISH, false); }
      break;
    }

    case GS_PAUSE:
      sPauseSel = (sPauseSel + navDelta(dt) + 2) % 2;
      if (backPressed()) enter(GS_RACE, false);
      else if (confirmPressed()) {
        if (sPauseSel == 0) enter(GS_RACE, false);
        else { sGP = false; autopilot = false; setCycle(TITLE_CYCLE); enter(GS_MENU); }
      }
      break;

    case GS_FINISH:
      updateDriving(dt);
      raceCycle();
      if (sStateT >= FINISH_TIME) { computeResults(); enter(GS_RESULTS); }
      break;

    case GS_RESULTS:
      updateDriving(dt);
      raceCycle();
      if (sStateT > 0.8f && confirmPressed()) {
        if (sGP) enter(GS_STANDINGS);
        else {
          sSelSel = currentCircuit;
          autopilot = false;
          setCycle(TITLE_CYCLE);
          enter(GS_SELECT);
        }
      }
      break;

    case GS_STANDINGS:
      updateDriving(dt);
      raceCycle();
      if (sStateT > 0.8f && confirmPressed()) {
        if (sGPRace < NUM_CIRCUITS - 1) {
          sGPRace++;
          startRace(sGPRace);
        } else {
          int order[NUM_RACERS];
          standingsOrder(order);
          if (order[0] == PLAYER_ID) { save_recordGPWin(difficulty); save_commit(); }
          autopilot = false;
          setCycle(TITLE_CYCLE);
          enter(GS_CHAMPION);
        }
      }
      break;

    case GS_CHAMPION:
      if (sStateT > 1.0f && confirmPressed()) { sGP = false; goTitle(); }
      break;

    default:
      goTitle();
      break;
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

static void render(float timeSec) {
  switch (sState) {
    case GS_TITLE:
      ui_drawTitle(timeSec);
      break;
    case GS_MENU: {
      static const char* const DIFF[3] = { "DIFFICULTY: EASY", "DIFFICULTY: NORMAL", "DIFFICULTY: HARD" };
      const char* items[4] = { "GRAND PRIX", "QUICK RACE", DIFF[iclamp(difficulty, 0, 2)], "BACK" };
      ui_drawMenuBackdrop(timeSec);
      ui_drawMenu("MAIN MENU", items, 4, sMenuSel, timeSec);
      break;
    }
    case GS_SELECT:
      ui_drawMenuBackdrop(timeSec);
      ui_drawCircuitSelect(sSelSel, timeSec);
      break;
    case GS_DEMO:
      renderRaceScene(timeSec);
      drawHUD(timeSec, sFps, sLcdFps, sFrameMs);
      ui_drawDemoBanner(sStateT);
      break;
    case GS_COUNTDOWN:
      renderRaceScene(timeSec);
      drawHUD(timeSec, sFps, sLcdFps, sFrameMs);
      ui_drawCountdown(sStateT);
      break;
    case GS_RACE:
      renderRaceScene(timeSec);
      drawHUD(timeSec, sFps, sLcdFps, sFrameMs);
      if (crashed) ui_drawCrash(crashTime);
      ui_drawGo(sGoT);
      if (sBannerT >= 0) ui_drawBanner(sBanner, sBannerCol, sBannerT);
      break;
    case GS_PAUSE:
      renderRaceScene(timeSec);
      drawHUD(timeSec, sFps, sLcdFps, sFrameMs);
      ui_drawPause(sPauseSel);
      break;
    case GS_FINISH:
      renderRaceScene(timeSec);
      drawHUD(timeSec, sFps, sLcdFps, sFrameMs);
      ui_drawFinish(sStateT, racers[PLAYER_ID].place);
      break;
    case GS_RESULTS:
      renderRaceScene(timeSec);
      ui_drawResults(sResults, sGP ? sRacePts : nullptr, sStateT);
      break;
    case GS_STANDINGS: {
      int order[NUM_RACERS];
      standingsOrder(order);
      renderRaceScene(timeSec);
      ui_drawStandings(order, sGPPoints, sGPRace + 1, NUM_CIRCUITS, sStateT);
      break;
    }
    case GS_CHAMPION: {
      int order[NUM_RACERS];
      standingsOrder(order);
      ui_drawMenuBackdrop(timeSec);
      ui_drawChampion(order[0], sGPPoints[order[0]], sStateT);
      break;
    }
    default: break;
  }
  if (sFade) ui_fade(sStateT, FADE_TIME);
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
}

void game_setup() {
  Serial.begin(115200);
#if defined(ARDUINO_ARCH_ESP32) && ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);   // logging must never block when no host reads the port
#endif
  delay(50);
  plat_report_memory("boot");

  input_init();
  // DMA bands + PSRAM snapshots first, then the big internal framebuffer
  // while the internal heap is still unfragmented.
  if (!display_init()) plat_log("display_init FAILED\n");
  if (!gfx_init()) plat_log("gfx_init FAILED: no memory for the framebuffer\n");
  initCarRenderer();
  if (!track_init()) plat_log("track_init FAILED\n");
  plat_report_memory("ready");

  save_init();
  difficulty = iclamp(saveData.difficulty, 0, 2);
  initPhysics();
  buildCircuit(0);
  race_init();
  goTitle();

  sLastMs = millis();
  sLastStatMs = sLastMs;
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

void game_debugStartRace(int circuit) {
  sGP = false;
  startRace(iclamp(circuit, 0, NUM_CIRCUITS - 1));
}

void game_debugStartGP() {
  startGP();
}
