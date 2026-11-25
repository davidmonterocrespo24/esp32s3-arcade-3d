// Headless frontend: runs the game on a virtual 30 Hz clock and writes
// selected frames as PPM images. No window, no Raylib — builds anywhere with
// a C++17 compiler (used to check the rendering without the hardware).
//
//   car_game_headless <outdir> <frames> [options] [frame numbers to save...]
//   options:  --cycle C       day cycle at start (0 day, 1 sunset, 2 night, 3 dawn)
//             --pos Z         teleport the player to track position Z
//             --steer A:B:V   steer V (-1..1) from frame A to frame B
//             --brake A:B     hold the brake from frame A to frame B
//             --title         keep the title screen (no auto start input)

#include <chrono>
#include <string>
#include <set>
#include <vector>
#include "Arduino.h"
#include "host_io.h"
#include "../colors.h"
#include "../physics.h"

extern void setup();
extern void loop();

struct Range { int a, b; float v; };

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

int main(int argc, char** argv) {
  g_emuVirtualClock = true;
  std::string out = argc > 1 ? argv[1] : ".";
  int frames = argc > 2 ? atoi(argv[2]) : 300;
  float cycle = -1, pos = -1;
  bool title = false;
  std::vector<Range> steer, brake;
  std::set<int> shots;
  for (int i = 3; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--cycle" && i + 1 < argc) cycle = (float)atof(argv[++i]);
    else if (a == "--pos" && i + 1 < argc) pos = (float)atof(argv[++i]);
    else if (a == "--title") title = true;
    else if (a == "--steer" && i + 1 < argc) {
      Range r; sscanf(argv[++i], "%d:%d:%f", &r.a, &r.b, &r.v); steer.push_back(r);
    } else if (a == "--brake" && i + 1 < argc) {
      Range r; sscanf(argv[++i], "%d:%d", &r.a, &r.b); r.v = 1; brake.push_back(r);
    } else shots.insert(atoi(a.c_str()));
  }

  setup();
  if (cycle >= 0) setDayCycle(cycle);
  if (pos >= 0) position = pos;

  const unsigned long stepMs = 33;
  double busy = 0;
  int timed = 0;
  for (int f = 0; f <= frames; f++) {
    InputState in = { 0, false, false, false };
    for (auto& r : steer) if (f >= r.a && f <= r.b) in.steer = r.v;
    for (auto& r : brake) if (f >= r.a && f <= r.b) in.brake = true;
    if (!title && f == 30) in.start = true;         // leave the title screen
    in.any = in.steer != 0 || in.brake || in.start;
    g_hostInput = in;

    g_emuNowMs += stepMs;
    auto t0 = std::chrono::steady_clock::now();
    loop();
    auto t1 = std::chrono::steady_clock::now();
    if (f > 5) { busy += std::chrono::duration<double, std::milli>(t1 - t0).count(); timed++; }

    if (shots.count(f)) {
      char name[64];
      snprintf(name, sizeof(name), "/frame_%04d.ppm", f);
      writePPM(out + name);
    }
  }
  printf("frames %d, avg host loop %.3f ms\n", timed, timed ? busy / timed : 0.0);
  return 0;
}
