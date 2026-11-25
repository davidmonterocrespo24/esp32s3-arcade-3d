# 2. Commit history: what was lost and when

Relevant commits, oldest first (dates from `git log`).

| Commit | Date | What happened |
| --- | --- | --- |
| `d752a50`…`2857afc` | 2026-02-10 | Monolithic sketch split into modules. The sky is three bands of `colSky1..3` from the palette, with stars at night: **day/night works**. |
| `0c7c569` | 2026-02-10 | Frame moved to a PSRAM sprite, `PROGMEM` tables, `TOTAL_SEGS` 300 → 200 "(RAM optimized)". |
| `684eed5` | 2026-02-10 | Parallax background: the sky is baked **once** into `bgSpr` with a fixed blue gradient; `drawSky()` stops using `colSky1..3`. **The sky loses the day/night cycle** (ground and road still change colour, so sky and ground no longer match). |
| `9fa6bb4` | 2026-02-11 | The baked sky becomes a fixed purple/orange sunset with a city skyline. From here on the sky is always "sunset". |
| `6f7d59c` | 2026-02-11 | Player car becomes a projected 3D model with `rotation = playerX * 0.5` — the start of the "crooked" car. |
| `75307ce` | 2026-02-12 | Gravity starts accumulating into `acceleration` (car stalls on long hills). |
| `2e5683f` | 2026-02-12 | The commit message says the camera lowers and the car moves forward inside tunnels. The diff only changes tunnel ceiling/wall drawing — **no camera change is in it**; that effect never landed. |
| `4112110` | 2026-02-13 | OBJ mesh + RGB565 texture for the car. |
| `4ca5019` | 2026-02-13 | Scanlines wider than 160 px are skipped (hides artifacts instead of fixing them). |
| `927201f` | 2026-02-13 | **AI competitors + race states** (countdown, finish, results screen) on `other_cars`. |
| `1d5cecd` | 2026-02-13 | PR #1 merges `other_cars` **at `ace1e9e`**, i.e. *before* `927201f`. |

## Lost work and where it stands

### Day/night cycle — restored

Lost in `684eed5`/`9fa6bb4`. This branch draws the sky every frame from the
active palette (dithered gradient, sun/moon, stars, clouds, two skyline layers
stored as ~2 KB height maps instead of a 150 KB sprite), with smooth blending
between four phases. See [page 6](06-world-rendering.md).

### AI competitors — still only on `origin/other_cars`

Commit `927201f` (+481/−49 lines: `CompetitorCar`, `updateCompetitors()`,
countdown/finish states, results screen) exists only on the remote branch
`origin/other_cars`; it was never merged into `road` or `main`. It is **not
ported** in this branch: it touches the same files that were rewritten here
(renderer, HUD, physics, loop). Porting it on top of the new renderer is a
separate task — the competitors would reuse `renderCar()` like the traffic
does.

### Car appearance — fixed

See [page 5](05-car-renderer.md).

### Tunnel camera effect — never existed

`2e5683f` describes it but does not implement it. Not added here.

## Branches at the time of the audit

```
road (default)         11fc9b9  <- base of graphics-overhaul
origin/main            09b4f6a  (merge of road, PR #2)
origin/other_cars      927201f  (unmerged AI competitors)
```
