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
`./car_game_headless <outdir> <frames> [--race C] [--nocd] [--gp] [--auto] [--diff D] [--cycle C]
[--pos Z] [--in A:B:SPEC] [--state NAME[:C]] [--dump N] [--every N] [frames to save...]` writes PPM
frames (virtual 30 Hz clock, deterministic); make contact sheets with
`python emulator/tools/sheet.py out.png 1 a.ppm b.ppm` and look at them. Checks in `emulator/tools/`:
`balance.sh` (autopilot vs rivals, every circuit × difficulty), `analyze_dump.py` (a `--dump 1`
log: overlaps, jitter, laps), `check_circuits.sh` (circuit rules + determinism). All options and
test recipes: `wiki/10-game-architecture.md`.

**Hardware target:** ESP32-S3 + ILI9341 320×240 SPI, Arduino IDE, Arduino-ESP32 core 4.0.0 (compile-verified).
Tools → PSRAM: OPI PSRAM, Tools → USB CDC On Boot: Enabled. Board/pins in `board.h`
(default: XIAO ESP32S3 Sense handheld, same unit as the mgs_reversing / sotn-decomp ports).
No TFT_eSPI: the panel is driven with ESP-IDF `esp_lcd` SPI + DMA. Command-line builds (WSL):
copy `*.ino *.cpp *.h` to a folder named `car_game` and run `arduino-cli compile -b
esp32:esp32:XIAO_ESP32S3:PSRAM=opi car_game` (dev board: `-b esp32:esp32:esp32s3:PSRAM=opi
--build-property "compiler.cpp.extra_flags=-DBOARD=2"`).

## Architecture

Pseudo-3D arcade racer. The same source compiles for ESP32 hardware and PC.
Everything draws into one software RGB565 framebuffer (`gfx.cpp`); only
`display_*.cpp` / `input_*.cpp` / `platform_*.cpp` (and `emulator/host_io.cpp`) are platform specific.

**Game flow** (`game.cpp`; `car_game.ino` only calls `game_setup()` / `game_loop()`):
TITLE → MENU → GRAND PRIX / QUICK RACE → SELECT (live flyover) → COUNTDOWN → RACE ⇄ PAUSE
(frozen frame from a PSRAM snapshot) → FINISH → RESULTS → (GP) STANDINGS → … → CHAMPION;
idle title → DEMO. Screen changes fade out, run on black, fade in. Per race frame:
1. `input_read()` → `handleInput(dt, in)` — steering, brake, turbo; the autopilot (the rivals'
   driver) drives in the demo, after the finish and with `--auto`
2. `updatePhysics(dt)` → `checkCollisions(dt)` (walls, rails, scenery, water) or `updateCrash(dt)`
3. `rivals_update(dt)` — every rival decides on the same snapshot, all move, then
   `physics_resolveContacts(dt)` resolves every car-car bump → `race_update(dt)` — progress,
   laps, finish, places
4. day cycle from race progress (`CircuitInfo::cycleStart → cycleEnd`, `applyDayCycle`)
5. Render: `drawSky()` → `drawRoad()` (calls `cars_prepare` / `cars_drawSlice`) →
   `drawPlayerCar()` → `cars_drawNear()` → `drawHUD()` → `ui_*` overlays
6. `display_present(gfx_fb)` — ESP32: copy to a PSRAM snapshot (or drop the frame if
   the panel is busy); a task on core 0 byte-swaps 8-line bands into DMA buffers and
   streams them, skipping bands identical to the previous frame

**Road pass** (`render_road.cpp`): pass 1 front-to-back projects segment boundaries and
records `clipRow` per segment; pass 2 back-to-front draws, per segment: road scanlines
(`gfx_rowRegionsTex`: textured near rows — asphalt, pavement + kerb, ground materials, water —
markings, AA edges, per-row fog), buildings (`render_building`, textured), tunnel walls/ceiling,
rails, the slice's rivals (`render_cars`), then its sprites (`render_scenery`), lamps and gantry
(they stand on the slice's near edge), then the tunnel portal — all clipped to `clipRow`. Camera
and boundary data are exported (`roadCam`, `roadProject`, `roadBX/BY/BZ`) so other modules
project identically; `drawRoad` uses only its parameters (the select screen's flyover).

**Textures** (`textures.cpp`): synthesised at boot from code (`textures_init()` in
`game_setup`): 12 wall + 8 ground textures with 4 mips, texels are 16 semantic slots turned into
an RGB565 palette per face / row (light, night windows, fog baked in). Buildings are drawn
`BLD_V_SCALE` (3) times taller with a floor every `BUILDING_FLOOR_H` (villas: 3× floors).

**Cars** (`render_player.cpp`): `renderCar(CarRender)` / `renderCarLod` — mesh `car2_mesh.h`
(smoothed normals), texture as 8-bit indices into an exact palette (paint flag for repainting),
per-vertex Gouraud diffuse + specular + sky reflection, perspective-correct UVs, bucket depth
sort, three LODs, turbo flame. Front of the model is +z; tail lights at (±0.85, 1.22, -3.2).
Rivals near the player are drawn from the player car's own camera, blending into the road
projection with distance (`render_cars.cpp`).

**Track** (`track.cpp`, `circuits.cpp`): `segments` (20-byte `Segment`, `MAX_SEGS` = 2400,
allocated once in PSRAM); every lookup wraps with `segCount`. `buildCircuit(id)` builds one of
three designed circuits from its section table (corner angles, closure solve, elevation keys)
and decorates its zones from a fixed seed (ground materials, buildings, rails, tunnels, lamps,
sprites with keep-out rules, minimap), then switches the palette theme and background.

## Key Constants

`config.h` holds only shared constants: world `ROAD_W=2000` (road half-width), `SEG_LEN=200`,
`DRAW_DIST=90`; lateral limits `RAIL_X`, `TUNNEL_WALL_X`, `BUILDING_OFFSET`; ground material
layout; `PLAYER_PAINT`. Module tunables live with their module: physics, turbo, contacts and
slipstream in `physics.h` (per-frame factors defined at 60 Hz and rescaled with
`powf(f, dt*60)`; gravity acts on speed), rival pace / personalities in `rivals.cpp`, day cycle in
`colors.h`, race rules and the car footprint (`CAR_HALF_W`, `CAR_LEN`) in `race.h`, building look
in `render_building.h`, texture density in `textures.h`, flow timings in `game.cpp`.

## Key Files

| File | Purpose |
|---|---|
| `config.h` / `board.h` / `structs.h` | Shared constants / pin maps / `Segment` + materials + building types |
| `game.cpp` / `ui.cpp` / `render_hud.cpp` | State machine / UI toolkit, screens and overlays / race HUD |
| `race.cpp` / `rivals.cpp` / `physics.cpp` | Racers, laps, places / rival AI + autopilot / car physics, contacts, walls, turbo |
| `circuits.cpp` / `track.cpp` | The three designed circuits + minimap / segment buffer and builders |
| `textures.cpp` | Procedural wall / ground / banner textures and their palettes |
| `gfx.cpp` | Framebuffer, polygons, spans, textured columns / spans / rows, blend/add, glow, 5x7 font |
| `display_esp32.cpp` | ILI9341 init + DMA scanout task (port pipeline) |
| `colors.cpp` | Palettes (RGB888) per theme blended into `pal` (RGB565 + light vectors) |
| `rendering.cpp` | Sky, sun/moon (`sky_sunX`), stars, clouds, theme backgrounds |
| `render_road.cpp` | Road / materials / markings / gantry + start lights / rails / lamps / tunnels / portals |
| `render_building.cpp` / `render_scenery.cpp` | Textured buildings of every type / 33 sprite types + hit widths |
| `render_cars.cpp` / `render_traffic.cpp` / `render_player.cpp` | Rivals in the road pass / one car from a blended camera / mesh renderer + player car |
| `save.cpp` | Records via `plat_save` / `plat_load` (NVS, written only between races) |

`wiki/10-game-architecture.md` is the module contract (interfaces, frame order, harness, memory
map); pages 11-14 document rivals/physics, city graphics, circuits/themes and flow/UI.

## Memory rules (ESP32-S3)

Framebuffer, textures, car texture and DMA bands live in internal SRAM; track segments, AI
tables, snapshots and other big, rarely touched buffers in PSRAM, allocated at runtime
(`plat_alloc_internal` / `plat_alloc_psram`, fall back to the other, log the placement):
`EXT_RAM_BSS_ATTR` is a no-op in this core and `malloc` above 4 KB lands in PSRAM. Allocate the
framebuffer early (`gfx_init()` right after `display_init()`). `HOT` = IRAM + noinline: put it
only on functions that contain a whole per-pixel / per-span / per-column loop; per-pixel helpers
stay `static inline` without it. Use `imin/imax/fminF/fmaxF` from `utils.h` — Arduino-ESP32 3.x+
`min/max` are std templates and reject mixed int/float arguments. Generate content with the
seeded `Rng` (`utils.h`), never `random()`. Guard every integer division (a divide by zero
reboots the S3); no stack arrays above ~1 KB.

## Generating Asset Headers

```bash
python assets/obj_to_header.py assets/Car2.obj > car2_mesh.h      # mesh + normals
python assets/png_to_rgb565.py assets/car2.png > car2_texture.h   # requires Pillow
```
