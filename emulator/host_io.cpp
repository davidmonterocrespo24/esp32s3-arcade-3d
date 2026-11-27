// display.h / input.h for the PC builds: frames go to a buffer the
// frontend (Raylib window or PPM writer) picks up; input comes from it.

#include <string.h>
#include "host_io.h"
#include "../display.h"

uint16_t   g_hostFrame[SCR_W * SCR_H];
bool       g_hostFrameNew = false;
InputState g_hostInput = { 0, 0, false, false, false, false, false, false };

DisplayStats displayStats;

bool display_init() {
  memset(&displayStats, 0, sizeof(displayStats));
  return true;
}

void display_present(const uint16_t* fb) {
  displayStats.offered++;
  displayStats.shown++;
  memcpy(g_hostFrame, fb, sizeof(g_hostFrame));
  g_hostFrameNew = true;
}

void input_init() {}

InputState input_read() { return g_hostInput; }
