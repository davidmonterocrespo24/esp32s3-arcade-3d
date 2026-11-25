# 8. Build, test and verification status

## 8.1 Hardware build (Arduino IDE)

1. Arduino-ESP32 core **3.x** (uses ESP-IDF 5 `esp_lcd`).
2. Board **XIAO_ESP32S3** (handheld) or **ESP32S3 Dev Module** (dev board).
   Select the pin map with `BOARD` in `board.h`.
3. **Tools → PSRAM → OPI PSRAM**.
4. **Tools → USB CDC On Boot → Enabled** (required on the XIAO; enforced by an
   `#error`).
5. No libraries needed (TFT_eSPI is no longer used).

First boot: open the serial monitor and check the `[mem]`, `gfx:` and `lcd:`
lines (see [architecture](03-architecture.md) and the troubleshooting table in
[the display page](04-display-pipeline.md)).

## 8.2 PC builds

```bash
cd emulator
make                 # Raylib window (Windows/MinGW)
make headless        # no window; writes PPM frames
./car_game_headless out 400 --cycle 2.2 300 400
```

Headless options: `--cycle C`, `--pos Z`, `--steer A:B:V`, `--brake A:B`,
`--title`. It runs on a virtual 30 Hz clock with a fixed seed, so runs are
reproducible.

## 8.3 What was verified

| Check | Result |
| --- | --- |
| Headless build (g++ 13, `-Wall`) | builds with no warnings |
| Rendering of title, day, sunset, night, dawn, tunnel approach, tunnel interior, crash, transitions | inspected frame by frame (screenshots in this wiki) |
| Driving sequence: acceleration, overtaking, lap completion, best/last lap | verified in a 900-frame run (lap ≈ 24.7 s, top ≈ 250–270 km/h) |
| Mesh generator output | vertices and indices identical to the old header; normals added |
| Raylib frontend (`emulator/main.cpp`) | compiles against the raylib 5.0 headers (`-fsyntax-only`); not run, Raylib is not installed on the dev machine |

## 8.4 Not verified yet

- **The ESP32 build has not been compiled yet.** The Arduino-ESP32 toolchain
  could not be installed on the development machine in time (the board
  index download was timing out). The code to watch is `display_esp32.cpp`,
  `input_esp32.cpp` and `platform_esp32.cpp` (ESP-IDF structs are filled field
  by field to stay compatible across IDF versions).
- **Nothing has run on the hardware yet**: ILI9341 init, `MADCTL` orientation,
  the DMA pipeline and real frame times must be checked on the device.
- Performance: on the PC a frame costs roughly 0.5–2 ms (noisy, WSL). The S3 is
  one to two orders of magnitude slower; the HUD shows the real `FPS / MS / LCD`
  numbers. The panel at 40 MHz caps the *shown* frame rate at ~32 fps for full
  updates (more with band skipping); the game keeps running at its own rate.

## 8.5 Performance work already done

- Framebuffer and texture in internal SRAM; hot loops in IRAM; `-O2` on the
  raster files.
- Triangle setup with plane-equation gradients (no per-scanline interpolation
  of 11 attributes); lighting only for visible vertices (~35 % faster car on
  the PC).
- Glow via LUT, fog via LUT, speedometer geometry precomputed, palette rebuilt
  only when it can change.

## 8.6 Next steps

1. Compile and flash; adjust `LCD_MADCTL` if needed; read the HUD numbers.
2. If the CPU is the bottleneck: lower `DRAW_DIST`, raise the traffic LOD
   threshold, or skip window quads earlier.
3. Port the AI competitors from `origin/other_cars` (`927201f`) onto the new
   renderer (they can reuse `renderCar()`).
4. Try 53/80 MHz SPI only with a soldered harness.
