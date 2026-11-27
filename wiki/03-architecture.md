# 3. New architecture and memory map

## 3.1 Layers

```
            ┌──────────────────────── game core (target-neutral) ───────────────────────┐
            │ car_game.ino   physics.cpp   track.cpp   colors.cpp                        │
            │ rendering.cpp  render_road.cpp  render_building.cpp                        │
            │ render_player.cpp  render_traffic.cpp  render_hud.cpp                      │
            │                         │ draws into                                       │
            │                    gfx.cpp  (uint16_t gfx_fb[320*240], RGB565)             │
            └─────────────┬──────────────────────────────┬──────────────────────────────┘
                          │ display_present(fb)          │ input_read()
          ESP32: display_esp32.cpp            ESP32: input_esp32.cpp
                 platform_esp32.cpp
          PC:    emulator/host_io.cpp  ->  main.cpp (Raylib) / main_headless.cpp (PPM)
```

Only `display_*`, `input_*` and `platform_*` know about the hardware. The PC
builds compile the same game sources, so the emulator shows exactly what the
panel receives (the old emulator re-rasterized every call with Raylib).

## 3.2 Frame flow (`car_game.ino`)

1. `input_read()`
2. Title state: `drawStartScreen()` until a control is touched (or 6 s).
3. Race state:
   1. `handleInput(dt, in)` — steering/brake, autopilot after 8 s idle
   2. `updatePhysics(dt)`, `checkCollisions()`
   3. parallax offset, `advanceDayCycle(speed * dt)`
   4. `drawSky()` → `drawRoad()` → `drawPlayerCar()` → `drawHUD()` (+ crash overlay)
   5. `display_present(gfx_fb)` — returns immediately on the ESP32

## 3.3 `gfx` — the software renderer

| Function | Notes |
| --- | --- |
| `gfx_hspan`, `gfx_fillRect`, `gfx_vspan` | 32-bit stores, clipped to the current clip rect |
| `gfx_fillPoly/Quad/Tri` | convex, float vertices, **pixel-centre rule**: neighbours never crack or double-draw |
| `*Mode(..., GFX_BLEND / GFX_ADD, alpha32)` | alpha blending and saturating additive light, 0..32 |
| `gfx_rowRegions` | one road scanline: colour regions at fractional x boundaries, boundary pixels blended by coverage (1-px AA) |
| `gfx_gradientRow`, `gfx_ditherRow` | 4×4 Bayer ordered dither: smooth gradients without RGB565 banding |
| `gfx_glow` | additive radial light via a falloff LUT (sun, lamps, tail lights) |
| `gfx_fillEllipse`, `gfx_circle` | |
| `gfx_text*` | 5×7 bitmap font, scale, drop shadow, centring |
| `gfx_setClip / gfx_setClipBottom` | the road pass clips everything of a segment to the rows nearer road has not covered |

Hot loops are tagged `HOT` (= `IRAM_ATTR` on the ESP32) and the raster files
are compiled with `#pragma GCC optimize("O2")` (Arduino builds with `-Os`).

## 3.4 Memory map (ESP32-S3, 8 MB octal PSRAM)

| Buffer | Placement | Size | Reason |
| --- | --- | --- | --- |
| Framebuffer `gfx_fb` | internal SRAM (PSRAM fallback) | 153,600 B | every fill writes here; no cache traffic |
| Car texture copy | internal SRAM | 32,768 B | random texel reads; paint flag stored in the green LSB |
| DMA bands ×2 | internal, `MALLOC_CAP_DMA` | 2 × 5,120 B | ping-pong SPI transfers |
| Track `segments[1000]` | internal `.bss` | 16,000 B | 16-byte packed `Segment` |
| Mesh work arrays | internal `.bss` | ~17 KB | per-vertex screen/light data, sort keys |
| Scanout snapshots ×2 | PSRAM | 2 × 153,600 B | immutable frame copy for the panel |
| Background height maps, stars, clouds | internal `.bss` | ~2.2 KB | replaces the 150 KB `bgSpr` |
| Mesh, normals, texture source, palettes, font | flash | ~45 KB | read-only |

Allocation order matters: `display_init()` (small DMA buffers + PSRAM
snapshots) then `gfx_init()` (the 150 KB internal block) while the internal
heap is still unfragmented. The serial log reports the result:

```
[mem] boot       internal free ...
gfx: 320x240 framebuffer (153600 B) in internal SRAM
lcd: ILI9341 40 MHz, 8-line DMA bands, snapshots in PSRAM
[mem] ready      internal free ...
```

If the internal allocation fails the framebuffer falls back to PSRAM (slower,
still correct) and the log says so.

## 3.5 Conventions

- Road projection (unchanged from the original):
  `x = 160 + scale * (worldX - camX) * 160`, `y = 120 - scale * (worldY - camY) * 120`,
  `scale = cameraDepth / z`, half road width `= scale * ROAD_W * 160`.
- Segment `n` spans boundary `n` (near) to `n+1` (far); `Segment::y` is the
  elevation of its far end.
- Car model: +z is the front, y = 0 the ground, tail lights at (±0.85, 1.22, −3.2).
- Colours: RGB565 native-endian in the framebuffer; the scanout swaps bytes.
- `imin/imax/fminF/fmaxF` instead of `min/max`: Arduino-ESP32 3.x defines
  them as `std::` templates that reject mixed int/float arguments (the emulator
  mock now does the same, so such errors show up on the PC too).
