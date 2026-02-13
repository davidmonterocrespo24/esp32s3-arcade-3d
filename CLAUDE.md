# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.
All code and comments are always in English.

## Build & Run

**Emulator (Windows — use this for all development):**
```bash
cd emulator/
make
./car_game_emu.exe
```

Kill the emulator before rebuilding:
```bash
taskkill /F /IM car_game_emu.exe 2>nul
rm -f car_game_emu.exe && make
```

**Hardware target:** ESP32-S3 with ILI9341 320×240 SPI display, compiled via Arduino IDE.

## Architecture

Pseudo-3D OutRun-style racing game. The same source compiles for ESP32 hardware and Windows (via `emulator/` mocks for Arduino API and TFT display using Raylib).

**Game loop** (`car_game.ino`):
1. `handleInput(dt)` — autopilot steering
2. `updatePhysics(dt)` — speed, acceleration, lateral drift
3. `checkCollisions()` — crashes with traffic/sprites/walls
4. Render: `drawSky()` → `drawRoad()` → `drawPlayerCar()` → `drawHUD()`
5. `spr.pushSprite(0,0)` — flip double buffer to display

**Rendering pipeline** (`render_road.cpp`):
- Single back-to-front loop per frame draws buildings, tunnel, and road together (painter's algorithm)
- `rCache[DRAW_DIST]` holds projected screen coordinates for each segment
- `rClip[DRAW_DIST]` holds the `maxy` horizon clipping value per segment
- `drawQuad()` = two `fillTriangle()` calls for trapezoids

**Player car** (`render_player.cpp`): OBJ mesh loaded as C static array (`car2_mesh.h`), texture as RGB565 header (`car2_texture.h`). Rendered with scanline affine texture mapping + painter's algorithm (insertion sort on Z).

**Track** (`track.cpp`): `segments[TOTAL_SEGS]` array. Each `Segment` stores curve, elevation Y, tunnel flag, building heights, colors. `buildTrack()` generates the full track on init.

## Key Constants (all in `config.h`)

World scale: `ROAD_W=2000` ≈ 10.5m real → 1 unit ≈ 5.25mm. `SEG_LEN=200`.

Physics tuning lives entirely in `config.h`:
- `SPEED_MULTIPLIER` — max speed (65.0 ≈ 246 km/h)
- `FRICTION` — coast-down rate (0.996 = ~3.3s from max)
- `CENTRIFUGAL` — curve drift force
- `GRAVITY_FACTOR` — hill acceleration effect

Building size/density: `BUILDING_H_MIN/MAX`, `BUILDING_SEG_MIN/MAX`, `BUILDING_GAP_MIN/MAX`.

## Key Files

| File | Purpose |
|---|---|
| `config.h` | **All tunable constants** — physics, buildings, screen, track |
| `structs.h` | `Segment`, `RenderPt`, `TrafficCar` data structures |
| `physics.cpp` | Speed, drift, gravity, collision detection, lap timing |
| `track.cpp` | Procedural track generation, building/tunnel placement |
| `render_road.cpp` | Road, tunnel, buildings, fog — unified back-to-front loop |
| `render_player.cpp` | 3D OBJ car with scanline texture rasterizer + shadow |
| `render_building.cpp` | 3D building geometry with window styles |
| `colors.cpp` | RGB565 palette, day/night/sunset color sets, fog lerp |
| `utils.cpp` | `easeInOut`, `findSegIdx`, `expFog`, `lerpF`, `clampF` |
| `emulator/TFT_eSPI.cpp` | Maps `spr.fillRect/fillTriangle` → Raylib draw calls |
| `car2_mesh.h` | 428 vertices, 312 triangles (generated from Car2.obj) |
| `car2_texture.h` | 128×128 RGB565 texture (generated from car2.png) |

## Generating Asset Headers

If `assets/Car2.obj` or `assets/car2.png` change, regenerate headers with Python:
```bash
# OBJ → C header
python assets/obj_to_header.py assets/Car2.obj > car2_mesh.h

# PNG → RGB565 header (requires Pillow)
python assets/png_to_rgb565.py assets/car2.png > car2_texture.h
```

## Emulator Internals

`emulator/car_game_wrapper.cpp` `#include`s `../car_game.ino` so it compiles as C++. The `Arduino.h` mock provides `millis()`, `random()`, `digitalRead()`. `TFT_eSPI.cpp` translates all sprite drawing calls to Raylib. Keyboard arrows map to the ESP32 GPIO buttons.
