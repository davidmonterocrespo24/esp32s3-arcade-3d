# 7. Physics and input

> This page describes the engine of the `graphics-overhaul` branch, before the
> game. The game's physics (contacts, turbo, slipstream, autopilot) are on
> [page 11](11-rivals-ai.md), its controls and input on [page 14](14-game-flow-and-ui.md).

## 7.1 Physics changes (`physics.cpp`)

| Change | Before | After |
| --- | --- | --- |
| Frame-rate independence | `FRICTION`, `LATERAL_FRICTION`, `ACCEL_DAMPING` applied once per frame | `powf(f, dt * 60)`: the constants keep their meaning at 60 Hz whatever the real frame rate |
| Gravity | added to `acceleration` (the throttle state) every frame — long climbs stalled the car | subtracted from `speed` directly |
| Throttle ramp | `ACCEL_RAMP` 180 | 420 (top speed in ~8 s) |
| Brake | none | `BRAKE_DECEL` while the brake is held |
| Off-road | free until `|x| > 2.4` | grass slows the car (and shakes it) |
| Tunnel wall crash | unreachable (speed reduced *before* the check) | crashes above 35 % of top speed |
| Scenery crash | unreachable | crashes above 25 % (bushes are soft) |
| Traffic collision | segment-index distance | distance along the track, wrap-aware |
| Traffic | random ±0.1 jitter | keeps lanes, changes lane now and then, eases across |

## 7.2 Input (`input.h`, `input_esp32.cpp`, `emulator/host_io.cpp`)

The original configured the buttons but never read them. Now:

| | XIAO handheld | Dev board | PC |
| --- | --- | --- | --- |
| Steer | analog stick X (centre calibrated at boot, travel learned while used, dead zone) | GPIO 17 / 16 | ← / → |
| Brake | D5 (GPIO 6) | — | ↓ / Space |
| Start | D7 (GPIO 44) or ladder START | both buttons | Enter |

Steering authority grows with speed (no turning on the spot). The throttle
stays automatic, as in the original design.

## 7.3 Autopilot / demo mode

- Active on the title-to-race transition when only START was pressed, and
  again after `AUTOPILOT_IDLE_MS` (8 s) without input; any steering or brake
  input hands the car back. The HUD shows `AUTO`.
- It follows the curve as before **and changes lane around slower cars ahead**
  (the old autopilot drove straight into traffic and crashed).

## 7.4 Track (`track.cpp`)

- `TOTAL_SEGS` 1000 (16-byte segments, 16 KB): ~25 s laps instead of ~3 s.
- Starts with a flat straight; the closing section eases the elevation
  **exactly** back to 0 (no step at the finish line).
- Two tunnels, city blocks on both sides, trees/bushes/rocks and marker posts
  in the gaps, street lamps in the city.
