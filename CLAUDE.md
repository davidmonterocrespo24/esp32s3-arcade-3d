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

**Headless renderer (no Raylib; works in WSL/Linux):** `cd emulator && make headless`, then
`./car_game_headless <outdir> <frames> [--race C] [--nocd] [--gp] [--auto] [--cycle C] [--pos Z]
[--in A:B:SPEC] [--dump N] [--every N] [frames to save...]` writes PPM frames; make contact
sheets with `python emulator/tools/sheet.py out.png 1 a.ppm b.ppm` and look at them. All options
and test recipes: `wiki/10-game-architecture.md`.

**Hardware target:** ESP32-S3 + ILI9341 320×240 SPI, Arduino IDE, Arduino-ESP32 core 4.0.0 (compile-verified).
Tools → PSRAM: OPI PSRAM, Tools → USB CDC On Boot: Enabled. Board/pins in `board.h`
(default: XIAO ESP32S3 Sense handheld, same unit as the mgs_reversing / sotn-decomp ports).
No TFT_eSPI: the panel is driven with ESP-IDF `esp_lcd` SPI + DMA.

## Architecture

Pseudo-3D arcade racer. The same source compiles for ESP32 hardware and PC.
Everything draws into one software RGB565 framebuffer (`gfx.cpp`); only
`display_*.cpp` / `input_*.cpp` (and `emulator/host_io.cpp`) are platform specific.

**Game flow** (`game.cpp`; `car_game.ino` only calls `game_setup()` / `game_loop()`):
TITLE → MENU → GRAND PRIX / QUICK RACE → SELECT → COUNTDOWN → RACE ⇄ PAUSE → FINISH →
RESULTS → (GP) STANDINGS → … → CHAMPION; idle title → DEMO. Per race frame:
1. `input_read()` → `handleInput(dt, in)` — steering, brake, turbo; the autopilot drives in
   the demo, after the finish and with `--auto`
2. `updatePhysics(dt)` → `checkCollisions(dt)` (or `updateCrash(dt)` while crashed)
3. `rivals_update(dt)` → `race_update(dt)` — progress, laps, finish, places
4. day cycle from race progress (`CircuitInfo::cycleStart → cycleEnd`, `applyDayCycle`)
5. Render: `drawSky()` → `drawRoad()` (calls `cars_prepare` / `cars_drawSlice`) →
   `drawPlayerCar()` → `cars_drawNear()` → `drawHUD()` → `ui_*` overlays
6. `display_present(gfx_fb)` — ESP32: copy to a PSRAM snapshot (or drop the frame if
   the panel is busy); a task on core 0 byte-swaps 8-line bands into DMA buffers and
   streams them, skipping bands identical to the previous frame

**Road pass** (`render_road.cpp`): pass 1 front-to-back projects segment boundaries and
records `clipRow` per segment; pass 2 back-to-front draws, per segment: road scanlines
(`gfx_rowRegions`: ground materials, kerbs, zebra crossings, start line, AA edges, per-row
fog), buildings, tunnel walls/ceiling, rails, sprites (`render_scenery`), lamps, gantry,
rivals (`render_cars`), tunnel portal — all clipped to `clipRow`. Camera and boundary data
are exported (`roadCam`, `roadProject`, `roadBX/BY/BZ`) so other modules project identically.

**Cars** (`render_player.cpp`): `renderCar(CarRender)` — mesh `car2_mesh.h` (with smoothed
normals), texture copied to internal RAM with a paint flag in the green LSB (traffic repaint),
per-vertex Gouraud diffuse + specular + sky reflection, perspective-correct UVs, triangle
gradients from plane equations, painter's sort. Front of the model is +z; tail lights at
(±0.85, 1.22, -3.2).

**Track** (`track.cpp`, `circuits.cpp`): `segments` (20-byte `Segment`, `MAX_SEGS` = 2400,
allocated once in PSRAM); every lookup wraps with `segCount`. `buildCircuit(id)` generates
one of three themed circuits from a fixed seed (layout, flags, ground materials, buildings,
rails, tunnels, sprites, minimap) and switches the palette theme and background.

## Key Constants

`config.h` holds only shared constants: world `ROAD_W=2000` (road half-width), `SEG_LEN=200`,
`DRAW_DIST=90`; lateral limits `RAIL_X`, `TUNNEL_WALL_X`, `BUILDING_OFFSET`; ground material
layout; `PLAYER_PAINT`. Module tunables live with their module: physics in `physics.h`
(per-frame factors defined at 60 Hz and rescaled with `powf(f, dt*60)`; gravity acts on speed),
day cycle in `colors.h`, race rules and the car footprint (`CAR_HALF_W`, `CAR_LEN`) in `race.h`.

## Key Files

| File | Purpose |
|---|---|
| `config.h` / `board.h` / `structs.h` | Shared constants / pin maps / `Segment` + materials + building types |
| `game.cpp` / `ui.cpp` / `render_hud.cpp` | State machine / screens and overlays / race HUD |
| `race.cpp` / `rivals.cpp` / `physics.cpp` | Racers, laps, places / rival AI / player physics, bumps, walls |
| `circuits.cpp` / `track.cpp` | The three circuits + minimap / segment buffer and builders |
| `gfx.cpp` | Framebuffer, polygons, spans, blend/add, glow, dithered rows, 5x7 font |
| `display_esp32.cpp` | ILI9341 init + DMA scanout task (port pipeline) |
| `colors.cpp` | Palettes (RGB888) blended into `pal` (RGB565 + light vectors), themes |
| `rendering.cpp` | Sky, sun/moon, stars, clouds, theme background height maps |
| `render_road.cpp` | Road / materials / tunnel / portal / rails / lamps / gantry |
| `render_building.cpp` / `render_scenery.cpp` | Buildings of every type / sprites + hit widths |
| `render_cars.cpp` / `render_traffic.cpp` / `render_player.cpp` | Rivals in the road pass / one car at a projected spot / mesh renderer + player car |
| `save.cpp` | Records via `plat_save` / `plat_load` |

`wiki/10-game-architecture.md` is the module contract (interfaces, ownership, harness).

## Memory rules (ESP32-S3)

Framebuffer, car texture and DMA bands live in internal SRAM; track segments and scanout
snapshots in PSRAM. Allocate the framebuffer early (`gfx_init()` after `display_init()`).
Mark per-pixel loops `HOT` (IRAM). Use `imin/imax/fminF/fmaxF` from `utils.h` — Arduino-ESP32
3.x `min/max` are std templates and reject mixed int/float arguments. Generate content with
the seeded `Rng` (`utils.h`), never `random()`. Guard every integer division (a divide by
zero reboots the S3); no stack arrays above ~1 KB.

## Generating Asset Headers

```bash
python assets/obj_to_header.py assets/Car2.obj > car2_mesh.h      # mesh + normals
python assets/png_to_rgb565.py assets/car2.png > car2_texture.h   # requires Pillow
```
