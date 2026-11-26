/*
  ═══════════════════════════════════════════════════════════════
  GAME FLOW (state machine) — car_game.ino only calls game_setup()
  and game_loop().

  TITLE -> MENU -> GRAND PRIX (3 circuits, points) / QUICK RACE -> SELECT
  -> COUNTDOWN (3-2-1-GO) -> RACE <-> PAUSE -> FINISH (cool-down) ->
  RESULTS -> GP: STANDINGS -> next circuit ... -> CHAMPION
             QUICK: back to SELECT
  TITLE idle 10 s -> DEMO (autopilot on a circuit), any input -> TITLE
  ═══════════════════════════════════════════════════════════════
*/

#ifndef GAME_H
#define GAME_H

#include <stdint.h>

enum GameState : uint8_t {
  GS_TITLE = 0, GS_DEMO, GS_MENU, GS_SELECT, GS_COUNTDOWN, GS_RACE, GS_PAUSE,
  GS_FINISH, GS_RESULTS, GS_STANDINGS, GS_CHAMPION, GS_COUNT };

void game_setup();
void game_loop();

GameState   game_state();
const char* game_stateName(GameState s);
float       game_stateTime();      // seconds in the current state
bool        game_isGP();
int         game_gpRace();         // 0-based race of the current Grand Prix
int         game_gpPoints(int racer);

// ═══════════════════════════════════════════════════════════════
//  HARNESS HOOKS (emulator/main_headless.cpp)
// ═══════════════════════════════════════════════════════════════
struct GameDebug {
  bool  autoDrive;     // --auto: the autopilot drives the player for the whole run
  bool  freezeCycle;   // --cycle: keep the day cycle at `cycle`
  float cycle;
  float teleportZ;     // --pos: >= 0 teleports the camera here right after GO
  bool  noCountdown;   // --nocd: GO at once
};
extern GameDebug gameDebug;

void game_debugStartRace(int circuit);   // --race C: straight to the grid + countdown
void game_debugStartGP();                // --gp: a Grand Prix from the first circuit

#endif // GAME_H
