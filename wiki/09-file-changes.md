# 9. File-by-file change list

## New files

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

## Rewritten

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

## Removed

| File | Why |
| --- | --- |
| `emulator/TFT_eSPI.h/.cpp` | The game no longer uses TFT_eSPI; the PC frontends display the real framebuffer |
| `emulator/SPI.h` | No longer included |

## Unchanged

`car2_texture.h`, `assets/png_to_rgb565.py`, `assets/Car2.obj`, `assets/car2.png`,
`emulator/car_game_wrapper.cpp`, `diagram.json`, images.
