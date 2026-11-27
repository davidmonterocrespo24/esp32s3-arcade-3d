// Headless frontend: runs the game on a virtual 30 Hz clock and writes
// selected frames as PPM images. No window, no Raylib — builds anywhere with
// a C++17 compiler (used to check rendering and race logic without hardware).
//
//   car_game_headless <outdir> <frames> [options] [frame numbers to save...]
//
//   --race C        skip title/menus: circuit C's grid + countdown
//   --nocd          with --race / --gp: no countdown (GO at frame 1)
//   --gp            start a Grand Prix from the first circuit
//   --auto          the autopilot drives the player car for the whole run
//   --diff D        difficulty 0 easy / 1 normal / 2 hard
//   --cycle C       override the day cycle (0 day 1 sunset 2 night 3 dawn) and freeze it
//   --pos Z         teleport the camera to track position Z right after GO
//   --in A:B:SPEC   input from frame A to B; SPEC = comma list of
//                   L,R,U,D,steer=V,brake,turbo,start,confirm,back
//   --dump N        every N frames print the state and every racer
//   --every N       save every Nth frame
//   --title         no automatic presses (the title falls into the demo after 10 s)
//   --save          enable save data (car_game_save.bin in the current folder)
//   --segs F        at frame F print the 40 segments ahead of the camera
//   --steer A:B:V / --brake A:B   (older forms of --in)
//   --state NAME[:C] jump straight to a screen before frame 0: TITLE, DEMO,
//                   MENU, SELECT, COUNTDOWN, RACE, PAUSE, FINISH, RESULTS,
//                   STANDINGS, CHAMPION, on circuit C (default 0; for
//                   STANDINGS the GP race 0..2). FINISH..CHAMPION fast-forward
//                   the race(s) with the autopilot first, without rendering.
//   --devkit        menus react like on the two-button dev board: a single
//                   L / R moves on release, confirm (both buttons) never moves
//
// Without --race / --gp / --title / --state the harness walks title -> menu
// -> QUICK RACE -> circuit 0 with scripted presses (frames 30, 50, 60, 80).
// With --gp it presses confirm every 45 frames on the results / standings
// screens so the whole Grand Prix runs.

#include <cctype>
#include <chrono>
#include <string>
#include <set>
#include <vector>
#include "Arduino.h"
#include "host_io.h"
#include "../colors.h"
#include "../physics.h"
#include "../race.h"
#include "../game.h"
#include "../circuits.h"
#include "../track.h"
#include "../utils.h"

extern void setup();
extern void loop();

static int segsAt = -1;   // --segs F

struct InSpec {
  int a, b;
  bool hasSteer = false, hasY = false;
  float steer = 0, y = 0;
  bool brake = false, turbo = false, start = false, confirm = false, back = false;
};

static void writePPM(const std::string& path) {
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) { fprintf(stderr, "cannot write %s\n", path.c_str()); return; }
  fprintf(f, "P6\n%d %d\n255\n", SCR_W, SCR_H);
  for (int i = 0; i < SCR_W * SCR_H; i++) {
    uint16_t c = g_hostFrame[i];
    unsigned char rgb[3] = {
      (unsigned char)(((c >> 11) & 31) * 255 / 31),
      (unsigned char)(((c >> 5) & 63) * 255 / 63),
      (unsigned char)((c & 31) * 255 / 31) };
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

static bool parseSpec(const char* arg, InSpec& s) {
  char spec[256];
  if (sscanf(arg, "%d:%d:%255s", &s.a, &s.b, spec) != 3) return false;
  std::string all = spec;
  size_t p = 0;
  while (p <= all.size()) {
    size_t q = all.find(',', p);
    if (q == std::string::npos) q = all.size();
    std::string t = all.substr(p, q - p);
    if (t == "L") { s.hasSteer = true; s.steer = -1; }
    else if (t == "R") { s.hasSteer = true; s.steer = 1; }
    else if (t == "U") { s.hasY = true; s.y = -1; }
    else if (t == "D") { s.hasY = true; s.y = 1; }
    else if (t.rfind("steer=", 0) == 0) { s.hasSteer = true; s.steer = (float)atof(t.c_str() + 6); }
    else if (t == "brake") s.brake = true;
    else if (t == "turbo") s.turbo = true;
    else if (t == "start") s.start = true;
    else if (t == "confirm") s.confirm = true;
    else if (t == "back") s.back = true;
    else if (!t.empty()) { fprintf(stderr, "unknown input '%s'\n", t.c_str()); return false; }
    p = q + 1;
  }
  return true;
}

static InSpec press(int f, const char* what) {
  InSpec s;
  parseSpec((std::to_string(f) + ":" + std::to_string(f) + ":" + what).c_str(), s);
  return s;
}

static void dumpState(int f) {
  printf("f=%d %s st=%.2f t=%.2f cyc=%.2f |", f, game_stateName(game_state()), game_stateTime(),
         raceTime, dayCycle);
  for (int i = 0; i < NUM_RACERS; i++) {
    const Racer& R = racers[i];
    printf(" %s P%d L%d z=%.0f x=%.2f v=%d%s%s |", R.name, R.place, R.lap, R.z, R.x,
           (int)(R.speed * 300.0f / maxSpeed), R.finished ? " FIN" : "", (i == 0 && crashed) ? " CRASH" : "");
  }
  printf("\n");
}

// The 40 segments ahead of the camera:
//   index: curve y | flags | ground L/R | build L/R (type:height) | sprites L/R
static void dumpSegments() {
  int b = findSegIdx(position);
  printf("segments ahead of the camera (base %d of %d):\n", b, segCount);
  for (int k = 0; k < 40; k++) {
    int i = segWrap(b + k);
    const Segment& s = segments[i];
    printf("  %4d c=%5.2f y=%7.0f f=%02X g=%d/%d b=%d:%d/%d:%d s=%d@%.2f/%d@%.2f\n", i, s.curve, s.y,
           s.flags, s.mat(0), s.mat(1), bldType(s.style[0]), s.build[0], bldType(s.style[1]), s.build[1],
           s.spr[0], s.sprX(0), s.spr[1], s.sprX(1));
  }
}

static void printResults() {
  ResultRow rows[NUM_RACERS];
  race_results(rows);
  printf("classification (%s, %d segments, lap %.0f units):\n", curCircuit().name, segCount, trackLength);
  for (int p = 0; p < NUM_RACERS; p++) {
    const Racer& R = racers[rows[p].racer];
    printf("  %d. %-6s %s %8.2f s  best lap %6.2f  laps %d  progress %.0f%s\n", p + 1, R.name,
           rows[p].finished ? "FIN" : "est", rows[p].time, rows[p].bestLap, R.lap, R.progress,
           game_isGP() ? "" : "");
  }
  if (game_isGP()) {
    printf("GP points:");
    for (int i = 0; i < NUM_RACERS; i++) printf(" %s=%d", racers[i].name, game_gpPoints(i));
    printf("\n");
  }
}

int main(int argc, char** argv) {
  g_emuVirtualClock = true;
  g_emuSaveEnabled = false;
  std::string out = argc > 1 ? argv[1] : ".";
  int frames = argc > 2 ? atoi(argv[2]) : 300;
  int raceC = -1, diff = -1, dumpN = 0, everyN = 0;
  segsAt = -1;
  float cycle = -1, pos = -1;
  bool title = false, gp = false, autoDrive = false, nocd = false;
  std::string jumpState;
  int jumpCircuit = 0;
  std::vector<InSpec> inputs;
  std::set<int> shots;
  for (int i = 3; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--cycle" && i + 1 < argc) cycle = (float)atof(argv[++i]);
    else if (a == "--pos" && i + 1 < argc) pos = (float)atof(argv[++i]);
    else if (a == "--race" && i + 1 < argc) raceC = atoi(argv[++i]);
    else if (a == "--diff" && i + 1 < argc) diff = atoi(argv[++i]);
    else if (a == "--dump" && i + 1 < argc) dumpN = atoi(argv[++i]);
    else if (a == "--every" && i + 1 < argc) everyN = atoi(argv[++i]);
    else if (a == "--segs" && i + 1 < argc) segsAt = atoi(argv[++i]);
    else if (a == "--title") title = true;
    else if (a == "--gp") gp = true;
    else if (a == "--auto") autoDrive = true;
    else if (a == "--nocd") nocd = true;
    else if (a == "--save") g_emuSaveEnabled = true;
    else if (a == "--devkit") gameDebug.devkitNav = true;
    else if (a == "--state" && i + 1 < argc) {
      std::string v = argv[++i];
      size_t c = v.find(':');
      jumpState = v.substr(0, c);
      for (auto& ch : jumpState) ch = (char)toupper((unsigned char)ch);
      if (c != std::string::npos) jumpCircuit = atoi(v.c_str() + c + 1);
    } else if (a == "--in" && i + 1 < argc) {
      InSpec s;
      if (parseSpec(argv[++i], s)) inputs.push_back(s);
    } else if (a == "--steer" && i + 1 < argc) {
      InSpec s;
      if (sscanf(argv[++i], "%d:%d:%f", &s.a, &s.b, &s.steer) == 3) { s.hasSteer = true; inputs.push_back(s); }
    } else if (a == "--brake" && i + 1 < argc) {
      InSpec s;
      if (sscanf(argv[++i], "%d:%d", &s.a, &s.b) == 2) { s.brake = true; inputs.push_back(s); }
    } else if (!a.empty() && a[0] != '-') {
      shots.insert(atoi(a.c_str()));
    } else {
      fprintf(stderr, "unknown option %s\n", a.c_str());
    }
  }

  gameDebug.autoDrive = autoDrive;
  gameDebug.noCountdown = nocd;
  gameDebug.teleportZ = pos;
  if (cycle >= 0) { gameDebug.freezeCycle = true; gameDebug.cycle = cycle; }

  setup();
  if (diff >= 0) difficulty = diff < 0 ? 0 : (diff > 2 ? 2 : diff);
  if (cycle >= 0) setDayCycle(cycle);
  if (!jumpState.empty()) {
    if (!game_debugJump(jumpState.c_str(), jumpCircuit)) {
      fprintf(stderr, "unknown state %s\n", jumpState.c_str());
      return 1;
    }
    printf("jumped to %s (circuit %d)\n", game_stateName(game_state()), jumpCircuit);
  } else if (raceC >= 0) game_debugStartRace(raceC);
  else if (gp) game_debugStartGP();
  else if (!title) {
    // walk title -> menu -> QUICK RACE -> circuit 0
    inputs.push_back(press(30, "confirm"));
    inputs.push_back(press(50, "D"));
    inputs.push_back(press(60, "confirm"));
    inputs.push_back(press(80, "confirm"));
  }

  const unsigned long stepMs = 33;
  double busy = 0;
  int timed = 0;
  int lastAutoPress = -100;
  for (int f = 0; f <= frames; f++) {
    InputState in = { 0, 0, false, false, false, false, false, false };
    for (auto& s : inputs) {
      if (f < s.a || f > s.b) continue;
      if (s.hasSteer) in.steer = s.steer;
      if (s.hasY) in.stickY = s.y;
      in.brake |= s.brake; in.turbo |= s.turbo; in.start |= s.start;
      in.confirm |= s.confirm; in.back |= s.back;
    }
    // A Grand Prix keeps going through the results and standings screens
    if (gp) {
      GameState st = game_state();
      if ((st == GS_RESULTS || st == GS_STANDINGS) && game_stateTime() > 1.5f && f - lastAutoPress > 45) {
        in.confirm = true;
        lastAutoPress = f;
      }
    }
    in.any = in.steer != 0 || in.stickY != 0 || in.brake || in.turbo || in.start || in.confirm || in.back;
    g_hostInput = in;

    g_emuNowMs += stepMs;
    auto t0 = std::chrono::steady_clock::now();
    loop();
    auto t1 = std::chrono::steady_clock::now();
    if (f > 5) { busy += std::chrono::duration<double, std::milli>(t1 - t0).count(); timed++; }

    if (dumpN > 0 && (f % dumpN) == 0) dumpState(f);
    if (segsAt == f) dumpSegments();
    if (shots.count(f) || (everyN > 0 && (f % everyN) == 0)) {
      char name[64];
      snprintf(name, sizeof(name), "/frame_%04d.ppm", f);
      writePPM(out + name);
    }
  }
  dumpState(frames);
  if (raceTime > 0) printResults();
  printf("frames %d, avg host loop %.3f ms\n", timed, timed ? busy / timed : 0.0);
  return 0;
}
