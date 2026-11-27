# 8. Build, test and verification status

> The build steps below still apply. The game's harness options, test recipes,
> tools and current build sizes are on [page 10](10-game-architecture.md)
> (sections 10.6-10.9).

## 8.1 Hardware build (Arduino IDE)

1. Arduino-ESP32 core **4.0.0** (compile-verified; 3.x should work too but was not tested).
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
| ESP32 build, XIAO handheld (`esp32:esp32:XIAO_ESP32S3:PSRAM=opi`, core 4.0.0) | compiles, no warnings: 407,937 B flash (12 %), 69,728 B static RAM (21 %), 257,952 B left for the heap |
| ESP32 build, dev board (`esp32:esp32:esp32s3:PSRAM=opi`, `-DBOARD=2`) | compiles: 437,157 B flash, 70,176 B static RAM |

## 8.4 Not verified yet

- The first ESP32 compile found one error, fixed: in newer ESP-IDF the
  `esp_lcd` SPI config uses `gpio_num_t` for `cs_gpio_num`/`dc_gpio_num`
  (explicit cast, still valid on older IDF). Core 3.x was not compiled.
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

1. Flash; adjust `LCD_MADCTL` if needed; read the HUD numbers and the `[mem]` log
   (the 150 KB framebuffer must land in internal SRAM; ~258 KB are free after statics).
2. If the CPU is the bottleneck: lower `DRAW_DIST`, raise the traffic LOD
   threshold, or skip window quads earlier.
3. Port the AI competitors from `origin/other_cars` (`927201f`) onto the new
   renderer (they can reuse `renderCar()`).
4. Try 53/80 MHz SPI only with a soldered harness.
