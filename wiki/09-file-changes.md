# 9. File-by-file change list

Two parts: **the game** (branch `full-game`, compared with
`graphics-overhaul`), then **the engine** (`graphics-overhaul`, compared with
the original code on `road`).

## Part A — the game (`full-game`)

### New files

| File | Purpose | Page |
| --- | --- | --- |
| `game.h`, `game.cpp` | State machine (title, demo, menu, select, countdown, race, pause, finish, results, standings, champion), fades, Grand Prix points, records, harness hooks | [14](14-game-flow-and-ui.md) |
| `ui.h`, `ui.cpp` | UI toolkit (run-length text with outline / gradient / slant, shaded panels, selection bar, car icons) and every screen; countdown lights shared with the world gantry | [14](14-game-flow-and-ui.md) |
| `race.h`, `race.cpp` | Racers, staggered grid, progress, laps, places, finish, estimated results | [11](11-rivals-ai.md) |
| `rivals.h`, `rivals.cpp` | Racing line and speed profile per circuit (PSRAM), driver model with personalities, gaps, overtaking, turbo, mistakes, catch-up; the autopilot uses the same driver | [11](11-rivals-ai.md) |
| `render_cars.h`, `render_cars.cpp` | Rivals in the road pass: car-camera / road-camera blend, culling, three LODs, slice of each car, cars nearer than the player | [11](11-rivals-ai.md) |
| `circuits.h`, `circuits.cpp` | The three designed circuits (section tables, closure solve, elevation keys), decoration zones, keep-out rules, minimap | [13](13-circuits-and-themes.md) |
| `render_scenery.h`, `render_scenery.cpp` | 33 sprite types with LODs (trees, palms, signs, billboards, lighthouse, traffic lights, bus stops, chalets, flags ...), footprints, hit widths | [13](13-circuits-and-themes.md) |
| `textures.h`, `textures.cpp` | Procedural textures generated at boot: 12 wall and 8 ground textures with mips, the START / FINISH banners; palettes per face, row and window | [12](12-city-graphics.md) |
| `save.h`, `save.cpp` | Best lap / race per circuit, GP wins, races; NVS on the ESP32, a file on the PC | [14](14-game-flow-and-ui.md) |
| `emulator/tools/balance.sh` | Autopilot balance matrix: every circuit × difficulty | [10](10-game-architecture.md) |
| `emulator/tools/analyze_dump.py` | Checks a `--dump 1` log: overlaps, contacts, lateral steps, jitter, overtakes, laps | [10](10-game-architecture.md) |
| `emulator/tools/circuit_dump.cpp`, `validate_circuits.py`, `check_circuits.sh` | Dumps every circuit with the game's code, validates layout and keep-out rules, checks determinism | [10](10-game-architecture.md) |
| `.gitattributes` | Shell scripts keep LF on a Windows checkout | |
| `wiki/10-14`, `wiki/img/*` | The game's documentation and screenshots | |

### Changed

| File | Main changes |
| --- | --- |
| `car_game.ino` | Only `setup()` → `game_setup()` and `loop()` → `game_loop()` |
| `structs.h` | 20-byte `Segment`: ground material per side, building type / variant / height per side, a sprite and its distance per side, flags (tunnel, lamps, crosswalk, start, rails); `MAX_SEGS` 2400 |
| `config.h` | Shared constants only: lateral limits (`RAIL_X`, `TUNNEL_WALL_X`, `BUILDING_OFFSET`, ...), ground-material layout; module tunables moved to their modules |
| `track.h/.cpp` | Segment buffer allocated once in PSRAM, `segCount`, `segWrap` / `segAt`, builders; the old generated track and traffic are gone |
| `colors.h/.cpp` | Theme palettes per time of day (sky, haze, background layers, ground materials, kerbs, props, water), facade colours, `setTheme`, `applyDayCycle` |
| `rendering.h/.cpp` | Theme backgrounds (city skyline with lit windows, sea with islands and headlands, snow-capped ranges), `bgTime`, `sky_sunX` |
| `render_road.h/.cpp` | Shared camera (`roadCam`, `roadProject`, boundary arrays); textured road rows and ground materials, sea with foam and sun glitter, zebra crossings and stop lines, chequered start line, gantry with banner and start lights, W-beam rails, street lamps, textured tunnels, portals per theme; cars drawn per slice |
| `render_building.h/.cpp` | Textured walls (perspective-correct column walker, mips, averaged columns at grazing angles), night windows lit one by one, roofs (flat, pitched, natural), every building type; villas with larger floors |
| `gfx.h/.cpp` | Textured column / span fillers and textured row regions (`HOT`), the font table exported |
| `render_player.h/.cpp` | Car texture as 8-bit indices, O(n) depth sort, LODs, turbo flame, car camera shared with near rivals, optional mesh copy to internal RAM |
| `render_traffic.h/.cpp` | `CarView` / `drawCarView` (one car from a blended camera) |
| `render_hud.h/.cpp` | New HUD: position, lap, gap, times, minimap from bit masks, speed dial, turbo meter, slipstream tag, position banners |
| `physics.h/.cpp` | Car-car contacts after every car moved, solid limits for every car, scenery and water, turbo, rocket start, slipstream, engine fixes, autopilot through the rivals' driver |
| `input.h`, `input_esp32.cpp` | `InputState` with menu axes, confirm, back; ADC read in its own task; ladder debounce; dev-board chords |
| `platform.h`, `platform_esp32.cpp` | `HOT` = IRAM + noinline; failed-allocation log, once-per-second status line, IRAM placement check, save via NVS |
| `display_esp32.cpp` | `convertBand` kept out of line (IRAM), its placement logged |
| `board.h` | Ladder thresholds for the handheld's buttons |
| `utils.h/.cpp` | Deterministic `Rng`, `wrapDelta`, `findSegIdx` over `segCount` |
| `emulator/main_headless.cpp`, `main.cpp`, `Arduino.*`, `host_io.cpp`, `Makefile`, `README.md` | Harness options (`--race`, `--gp`, `--auto`, `--state`, `--in`, `--dump`, ...), virtual clock, save file, P key, every `../*.cpp` compiled |
| `readme.md`, `CLAUDE.md` | The game: features, controls, structure, memory, build |

### Removed

| What | Why |
| --- | --- |
| Traffic (`TrafficCar`, `initTraffic`, `MAX_CARS`) | Replaced by the five rivals |
| The generated track (`buildTrack`, `RANDOM_TRACK`, `TOTAL_SEGS`) | Replaced by the three designed circuits |
| Title screen and crash overlay in `render_player.cpp` | Moved to `ui.cpp` |

## Part B — the engine (`graphics-overhaul`)

### New files

| File | Purpose |
| --- | --- |
| `platform.h` | `HOT` (IRAM) / `FAST_DATA` macros, allocation and logging helpers |
| `board.h` | Pin maps: XIAO handheld (default) and original dev board; CDC-on-boot guard |
| `gfx.h`, `gfx.cpp` | Software framebuffer and rasterizer, blending, glow, dithered rows, road row regions, 5×7 font |
| `display.h` | `display_init()` / `display_present()` / `displayStats` |
| `display_esp32.cpp` | ILI9341 init + SPI DMA scanout task on core 0 (ports' pipeline) |
| `platform_esp32.cpp` | `heap_caps` allocation (internal / PSRAM), logging, memory report |
| `input.h`, `input_esp32.cpp` | `InputState`; stick/buttons for the handheld, two buttons for the dev board |
| `emulator/host_io.h/.cpp` | PC implementations of `display_*` and `input_*` |
| `emulator/main_headless.cpp` | Headless frontend writing PPM frames |
| `wiki/` | This documentation |

### Rewritten

| File | Main changes |
| --- | --- |
| `car_game.ino` | Title/race states, real input, frame stats, `display_present()`, memory reports; no TFT_eSPI |
| `config.h` | `DRAW_DIST` 90, `TOTAL_SEGS` 1000, day-cycle constants, brake/steer/autopilot constants, `TUNNEL_H`, `PLAYER_PAINT`, `SHOW_FPS`; pins moved to `board.h` |
| `structs.h` | `Segment` packed to 16 B (buildings as height units + style byte, lamp flag); `TrafficCar.targetOffset` |
| `colors.h/.cpp` | RGB565 blend/add/scale helpers; 4 palettes with smooth blending into `pal` |
| `rendering.h/.cpp` | Palette-driven sky, sun/moon, stars, clouds, parallax height-map skyline |
| `render_road.h/.cpp` | Two-pass road: scanline regions with AA, per-row fog, tunnels + portal, sprites, lamps, exact traffic placement |
| `render_building.h/.cpp` | Sun-side shading, window styles, night lights, neon, fog |
| `render_player.h/.cpp` | `renderCar()` mesh renderer (see page 5), player car, title screen, crash overlay |
| `render_traffic.h/.cpp` | Traffic through `renderCar()` with repaint + LOD |
| `render_hud.h/.cpp` | Translucent HUD, segmented speedometer, progress bar, FPS counter |
| `physics.h/.cpp` | Frame-rate independence, gravity fix, brake, off-road, autopilot with traffic avoidance, collision fixes |
| `track.h/.cpp` | Longer track, exact elevation closure, 2 tunnels, styles, scenery, lamps, lane-based traffic |
| `utils.h/.cpp` | `imin/imax/iclamp/fminF/fmaxF`, `hash32` |
| `assets/obj_to_header.py` | Emits smoothed vertex normals (crease angle 50°) |
| `car2_mesh.h` | Regenerated: same vertices/indices + `car2_normals[]` |
| `emulator/Arduino.h/.cpp` | No Raylib dependency; virtual clock; `std::min/max` like the ESP32 core; host allocation/logging |
| `emulator/main.cpp` | Raylib frontend uploading the RGB565 framebuffer (×3, nearest) |
| `emulator/Makefile` | `make` (Raylib) and `make headless` targets |
| `readme.md`, `CLAUDE.md`, `emulator/README.md` | Updated to the new architecture |
| `.gitignore` | Headless binary, PPM files, emulator screenshot |

### Removed

| File | Why |
| --- | --- |
| `emulator/TFT_eSPI.h/.cpp` | The game no longer uses TFT_eSPI; the PC frontends display the real framebuffer |
| `emulator/SPI.h` | No longer included |

### Unchanged

`car2_texture.h`, `assets/png_to_rgb565.py`, `assets/Car2.obj`, `assets/car2.png`,
`emulator/car_game_wrapper.cpp`, `diagram.json`, images.
