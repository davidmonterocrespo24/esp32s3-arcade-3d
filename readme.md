# ESP32 Pseudo-3D Racing Game

An arcade-style pseudo-3D racing game for the ESP32-S3 microcontroller with an ILI9341 320×240 SPI display. The same source compiles for the ESP32 hardware and for the PC (a Raylib emulator and a headless renderer).

---

## Screenshots

### Video Demo

![Game Demo](img/game.gif)

### Images

<img src="img/hardesp32.png" alt="ESP32 Hardware" >

![Game Screenshot 1](img/Captura%20de%20pantalla%202026-02-13%20112753.png)

![Game Screenshot 2](img/Captura%20de%20pantalla%202026-02-13%20112759.png)

![Game Screenshot 3](img/image.png)

---

## Features

- **Pseudo-3D road** — per-scanline road with anti-aliased edges, rumble strips, edge lines and lane dashes; per-scanline exponential fog
- **Software renderer** — own RGB565 framebuffer and rasterizer (`gfx.cpp`): gap-free polygons, alpha blending, additive lights, dithered gradients, bitmap font
- **3D cars** — OBJ mesh (428 vertices, 312 triangles), perspective-correct texture mapping, smoothed normals with Gouraud diffuse + specular + sky reflection, contact shadow, tail/brake lights
- **Traffic** — 10 cars rendered with the same mesh (repainted), seen from their real angle, lane changes, LOD for far cars
- **Day → sunset → night → dawn cycle** — smooth palette blending: sky gradient, sun/moon, stars, clouds, skyline windows that light up at night, street lamps, headlight beam
- **City and countryside** — buildings with lit/shaded faces and window patterns, trees, bushes, rocks, marker posts, street lamps
- **Tunnels** — portal façade, lit ceiling, light pools, darkness fog, exit opening
- **Physics** — frame-rate independent acceleration, braking, coast-down, centrifugal drift, hill gravity, off-road slowdown
- **Controls** — real steering (stick or buttons) with autopilot demo mode after 8 s idle (the autopilot avoids traffic)
- **HUD** — translucent panels, segmented speedometer, lap/best/last times, lap progress bar, FPS counter

---

## Hardware

Two boards are supported; pick one in `board.h` (`BOARD`).

**Handheld console (default)** — the same unit as the [mgs_reversing](https://github.com/davidmonterocrespo24/mgs_reversing) / [sotn-decomp](https://github.com/davidmonterocrespo24/sotn-decomp) ports:

| Component | Details |
| --------- | ------- |
| MCU | Seeed XIAO ESP32S3 Sense, 240 MHz dual-core, 8 MB octal PSRAM |
| Display | ILI9341 320×240 SPI @ 40 MHz — SCK=7, MOSI=9, CS=4, DC=43, RST=5 |
| Input | Analog stick X (GPIO1) steers, D5 (GPIO6) brakes, D7 (GPIO44) / ladder START starts |

**Original dev board** — ESP32-S3, ILI9341 on SCK=12, MOSI=11, MISO=13, CS=10, DC=9, RST=8, backlight GPIO39, buttons GPIO17 (left) / GPIO16 (right).

### Building for the hardware (Arduino IDE)

1. Board: **XIAO_ESP32S3** (or *ESP32S3 Dev Module* for the dev board), Arduino-ESP32 core 4.0.0 (compile-verified)
2. **Tools → PSRAM → OPI PSRAM**
3. **Tools → USB CDC On Boot → Enabled** (mandatory on the XIAO: GPIO43 is the panel's D/C line, and with CDC disabled `Serial` would take it)
4. Compile and upload. **No TFT_eSPI library is needed**: the panel is driven with ESP-IDF's `esp_lcd` SPI + DMA.

The serial monitor prints where each buffer was placed (`[mem] ...`, `gfx: ... framebuffer in internal SRAM`).

---

## Controls

| Action | Handheld | Dev board | Emulator |
| ------ | -------- | --------- | -------- |
| Steer | Stick X | GPIO 17 / 16 buttons | ← / → |
| Brake | D5 | — | ↓ or Space |
| Start | D7 or ladder START | both buttons | Enter |
| Throttle | Automatic | Automatic | Automatic |

With no input for 8 seconds the autopilot takes over (demo mode); touching the controls gives the car back.

---

## Build — PC

### Emulator (Windows, Raylib)

```bash
cd emulator/
make
./car_game_emu.exe
```

Kill and rebuild:

```bash
taskkill /F /IM car_game_emu.exe 2>nul
rm -f car_game_emu.exe && make
```

### Headless renderer (any OS with g++, no Raylib)

Runs the game on a virtual 30 Hz clock and writes frames as PPM images — useful to check rendering changes without the hardware:

```bash
cd emulator/
make headless
./car_game_headless out 400 --cycle 2.2 300 400     # night, save frames 300 and 400
./car_game_headless out 60 --pos 64000 60           # in front of the first tunnel
```

Options: `--cycle C` (0 day, 1 sunset, 2 night, 3 dawn), `--pos Z`, `--steer A:B:V`, `--brake A:B`, `--title`.

---

## Project Structure

```text
├── car_game.ino            # Setup, game states, main loop
├── config.h                # Tunable constants (physics, track, cycle, buildings)
├── board.h                 # Pin maps: XIAO handheld / original dev board
├── platform.h              # HOT (IRAM) / allocation helpers
├── gfx.cpp/.h              # Software framebuffer, rasterizer, font
├── display.h               # display_init / display_present
├── display_esp32.cpp       # ILI9341 over SPI DMA, scanout task on core 0
├── input.h, input_esp32.cpp
├── platform_esp32.cpp      # heap_caps allocation + logging
├── structs.h               # Segment (16 B), TrafficCar
├── colors.cpp/.h           # RGB565 helpers, day/sunset/night/dawn palettes
├── track.cpp/.h            # Track, tunnels, city, scenery, traffic setup
├── physics.cpp/.h          # Speed, drift, gravity, collisions, autopilot, laps
├── rendering.cpp/.h        # Sky, sun/moon, stars, clouds, parallax skyline
├── render_road.cpp/.h      # Road rows, tunnel, portal, sprites, lamps, traffic
├── render_building.cpp/.h  # Buildings with windows
├── render_player.cpp/.h    # Textured 3D car renderer, player, title, crash
├── render_traffic.cpp/.h   # Traffic cars (mesh + LOD)
├── render_hud.cpp/.h       # HUD and speedometer
├── utils.cpp/.h
├── car2_mesh.h             # Generated: mesh + smoothed normals
├── car2_texture.h          # Generated: 128x128 RGB565 texture
├── assets/                 # Car2.obj, car2.png, generators
└── emulator/
    ├── Arduino.h/.cpp      # Arduino API mock + host allocation/logging
    ├── host_io.h/.cpp      # display/input hooks for the PC frontends
    ├── main.cpp            # Raylib window frontend
    ├── main_headless.cpp   # PPM frame writer frontend
    └── car_game_wrapper.cpp
```

---

## Memory layout (ESP32-S3)

| Buffer | Where | Size | Why |
| ------ | ----- | ---- | --- |
| Framebuffer | internal SRAM | 150 KB | every fill writes here; no cache traffic |
| Car texture copy | internal SRAM | 32 KB | random texel reads; flash/PSRAM share the cache |
| DMA bands ×2 | internal, DMA-capable | 2×5 KB | ping-pong SPI transfers |
| Track | internal (.bss) | 16 KB | 1000 packed 16-byte segments |
| Scanout snapshots ×2 | PSRAM | 2×150 KB | immutable copy for the panel, written once per frame |
| Mesh, palettes, font | flash | ~13 KB | read-only |

Hot raster loops are placed in IRAM (`HOT`), and the scanout runs on core 0 while the game runs on core 1 — the same split used in the MGS/SOTN ports.

---

## Regenerating Asset Headers

```bash
# OBJ mesh -> C header with smoothed normals (Python 3)
python assets/obj_to_header.py assets/Car2.obj > car2_mesh.h

# PNG texture -> RGB565 C header (requires Pillow)
python assets/png_to_rgb565.py assets/car2.png > car2_texture.h
```
