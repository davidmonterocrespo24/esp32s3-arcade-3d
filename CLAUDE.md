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
`./car_game_headless <outdir> <frames> [--cycle C] [--pos Z] [--steer A:B:V] [frames to save...]`
writes PPM frames. Use it to check rendering changes visually.

**Hardware target:** ESP32-S3 + ILI9341 320×240 SPI, Arduino IDE, Arduino-ESP32 core 3.x.
Tools → PSRAM: OPI PSRAM, Tools → USB CDC On Boot: Enabled. Board/pins in `board.h`
(default: XIAO ESP32S3 Sense handheld, same unit as the mgs_reversing / sotn-decomp ports).
No TFT_eSPI: the panel is driven with ESP-IDF `esp_lcd` SPI + DMA.

## Architecture

Pseudo-3D arcade racer. The same source compiles for ESP32 hardware and PC.
Everything draws into one software RGB565 framebuffer (`gfx.cpp`); only
`display_*.cpp` / `input_*.cpp` (and `emulator/host_io.cpp`) are platform specific.

**Game loop** (`car_game.ino`): title state → race state.
1. `input_read()` → `handleInput(dt, in)` — manual steering, autopilot after 8 s idle
2. `updatePhysics(dt)` — speed, gravity, drift, traffic, laps (frame-rate independent)
3. `checkCollisions()`
4. `advanceDayCycle(distance)` — palette blend day → sunset → night → dawn
5. Render: `drawSky()` → `drawRoad()` → `drawPlayerCar()` → `drawHUD()`
6. `display_present(gfx_fb)` — ESP32: copy to a PSRAM snapshot (or drop the frame if
   the panel is busy); a task on core 0 byte-swaps 8-line bands into DMA buffers and
   streams them, skipping bands identical to the previous frame

**Road pass** (`render_road.cpp`): pass 1 front-to-back projects segment boundaries and
records `clipRow` per segment; pass 2 back-to-front draws, per segment: road scanlines
(`gfx_rowRegions`, AA edges, per-row fog), buildings, tunnel walls/ceiling, sprites,
lamps, traffic, tunnel portal — all clipped to `clipRow`.

**Cars** (`render_player.cpp`): `renderCar(CarRender)` — mesh `car2_mesh.h` (with smoothed
normals), texture copied to internal RAM with a paint flag in the green LSB (traffic repaint),
per-vertex Gouraud diffuse + specular + sky reflection, perspective-correct UVs, triangle
gradients from plane equations, painter's sort. Front of the model is +z; tail lights at
(±0.85, 1.22, -3.2).

**Track** (`track.cpp`): `segments[TOTAL_SEGS]` (16-byte `Segment`). `buildTrack()` generates
curves/hills, closes the elevation loop exactly, marks 2 tunnels, city blocks, scenery, lamps.

## Key Constants (all in `config.h`)

World: `ROAD_W=2000` (road half-width), `SEG_LEN=200`, `DRAW_DIST=90`, `TOTAL_SEGS=1000`.
Physics per-frame factors (`FRICTION`, `LATERAL_FRICTION`, `ACCEL_DAMPING`) are defined at
60 Hz and rescaled with `powf(f, dt*60)`. Gravity acts on speed (not on the throttle state).
Day cycle: `CYCLE_PHASE_DIST`, `CYCLE_BLEND`, `CYCLE_START`. Player paint: `PLAYER_PAINT`.

## Key Files

| File | Purpose |
|---|---|
| `config.h` / `board.h` | Tunables / pin maps |
| `gfx.cpp` | Framebuffer, polygons, spans, blend/add, glow, dithered rows, 5x7 font |
| `display_esp32.cpp` | ILI9341 init + DMA scanout task (port pipeline) |
| `colors.cpp` | Palettes (RGB888) blended into `pal` (RGB565 + light vectors) |
| `rendering.cpp` | Sky, sun/moon, stars, clouds, parallax skyline height maps |
| `render_road.cpp` | Road / tunnel / portal / sprites / lamps / traffic placement |
| `render_building.cpp` | Building faces and windows |
| `render_player.cpp` | Mesh renderer, player car, title screen, crash overlay |
| `physics.cpp` | Physics, autopilot with traffic avoidance, collisions, laps |

## Memory rules (ESP32-S3)

Framebuffer, car texture, DMA bands and track live in internal SRAM; scanout snapshots in
PSRAM. Allocate the framebuffer early (`gfx_init()` after `display_init()`). Mark per-pixel
loops `HOT` (IRAM). Use `imin/imax/fminF/fmaxF` from `utils.h` — Arduino-ESP32 3.x `min/max`
are std templates and reject mixed int/float arguments.

## Generating Asset Headers

```bash
python assets/obj_to_header.py assets/Car2.obj > car2_mesh.h      # mesh + normals
python assets/png_to_rgb565.py assets/car2.png > car2_texture.h   # requires Pillow
```
