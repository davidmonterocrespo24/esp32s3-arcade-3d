// Shared state between the game's display/input hooks and the PC frontends.
#pragma once
#include <stdint.h>
#include "../config.h"
#include "../input.h"

extern uint16_t   g_hostFrame[SCR_W * SCR_H];   // last presented frame (RGB565)
extern bool       g_hostFrameNew;
extern InputState g_hostInput;                  // filled by the frontend
