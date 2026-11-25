# 1. Audit of the original code

Baseline: commit `11fc9b9` on branch `road`.

## 1.1 Memory management

| # | Finding | Cost |
| --- | --- | --- |
| M1 | The whole frame was composed in a `TFT_eSprite` (`spr`, 320×240×16 bpp = **153,600 B**) placed in **PSRAM**. Every primitive went through the sprite API into PSRAM, through the cache that flash and PSRAM share on the S3. | Every pixel write is a cached PSRAM write. |
| M2 | The parallax sky was a second sprite (`bgSpr`, 640×120×2 = **153,600 B**, PSRAM), drawn **once** at boot and copied with two `pushToSprite()` calls per frame. | ~77 KB of PSRAM→PSRAM copy per frame, and the sky could never change colour again (see [history](02-commit-history.md)). |
| M3 | The car texture (128×128 RGB565 = 32 KB) was read **from flash** per texel (`pgm_read_word`). | Random texel reads miss the cache and evict the PSRAM framebuffer lines that the same cache holds. |
| M4 | The car rasterizer wrote each pixel with `spr.drawPixel()`. | A function call, a clip test and a byte swap per pixel, into PSRAM. |
| M5 | `spr.pushSprite(0,0)` sent 153,600 B over SPI **blocking**: ~31 ms at 40 MHz (~15 ms at 80 MHz) with the CPU idle. | No overlap between rendering and transfer; the second core was unused. |
| M6 | `Segment` was 32 bytes (float/int/bool padding). `TOTAL_SEGS` was cut from 300 to 200 in `0c7c569` "(RAM optimized)". | A lap was 40,000 units = **~3 s at top speed**. |
| M7 | Small per-frame work arrays (`px/py/pz[428]`, `order/zdepth[312]`) were static. | Fine — kept the same idea. |
| M8 | Emulator: each primitive was wrapped in `BeginTextureMode()/EndTextureMode()` (a GPU flush per call, thousands per frame for the car) and rasterized by Raylib, not by TFT_eSPI. | Slow, and the emulator did not show what the hardware showed. |

## 1.2 Rendering

- **Road edges** were drawn as up to 3 stair-stepped rectangles per segment
  band, so the road had jagged edges at every distance.
- The colours computed in the projection pass (`grass`, `road`, `rumble`,
  `lane`) were never used (dead code).
- Off-by-one: the band between boundaries `n-1` and `n` was drawn with the
  colours/flags of segment `n`.
- **Sprites and traffic** were drawn in a third pass on top of everything, so
  trees and cars appeared over buildings that should hide them.
- **Traffic** was snapped to the start of its segment (cars moved in steps of
  one segment) and `drawTrafficCar()` ignored its `clipY` argument (cars showed
  through hills).
- **No fog on buildings**: they popped in at full colour at the end of the draw
  distance. `DRAW_DIST` was only 40 segments.
- The tunnel was 4500 units tall (4.5× the camera height, ~23 m).
- Building roofs were drawn even when above eye level (seen from below).
- Fog was applied per segment (visible bands).

## 1.3 The player car (`render_player.cpp`)

Detailed in [page 5](05-car-renderer.md). In short: yaw came from the lateral
position (`rotY = playerX * 0.5`), the eye sat 6.5 units from a 6.5-unit-long
model (wedge-like perspective), the shadow moved with `playerX` while the car
stayed centred, scanlines wider than 160 px and triangles near the screen edge
were skipped, texturing was affine with integer edges (swimming textures,
cracks), and lighting was computed in object space (it never changed when the
car turned).

## 1.4 Physics

- **Frame-rate dependent**: `FRICTION` (0.996), `LATERAL_FRICTION` (0.90) and
  `ACCEL_DAMPING` (0.97) were applied once per frame, so the car handled
  differently at 20 fps than at 60 fps.
- **Gravity accumulated into the throttle**: `acceleration += -slope *
  GRAVITY_FACTOR * dt` every frame (since `75307ce`). On a long climb the
  throttle state went negative and the car crawled to a stop.
- **Dead crash code**: tunnel walls did `speed *= 0.3` and then crashed only if
  `speed > 0.35 * maxSpeed` (impossible); scenery did `speed *= 0.2` and then
  checked `> 0.25 * maxSpeed` (impossible).
- **Track loop**: the closing hill was clamped to ±14 and the rest padded with
  `y = 0` segments, leaving a step at the finish line whenever the track ended
  higher than 2800 units.

## 1.5 Input

`BTN_LEFT`/`BTN_RIGHT` were configured with `pinMode()` but **never read**:
`handleInput()` was an autopilot only, even though the README and the start
screen said "LEFT / RIGHT buttons to steer". The autopilot did not look at
traffic and regularly drove into it.

## 1.6 Documentation vs. behaviour

The README promised "smooth colour palette transitions every ~180 km". The
code switched the palette abruptly every 180,000 units (~14 s at top speed),
and since `684eed5` the sky ignored the palette entirely.
