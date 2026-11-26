# PC builds of the ESP32 Car Game

The game draws into its own RGB565 software framebuffer (`../gfx.cpp`) on every
platform, so the PC builds show exactly the pixels the ESP32 sends to the panel.
Only the frontends differ:

| Target | Command | Needs |
| ------ | ------- | ----- |
| `car_game_emu.exe` | `make` | MinGW g++ + [Raylib](https://www.raylib.com/) (`RAYLIB_PATH`, default `C:/raylib`) |
| `car_game_headless` | `make headless` | any C++17 g++ (Linux/WSL/MinGW) |

The Makefile compiles every `../*.cpp` (the ESP32-only files are guarded by
`ARDUINO_ARCH_ESP32` and compile to nothing), so new game files need no change.

## Emulator controls

* **Left / Right**: steer
* **Up / Down**: menus
* **Space / Down**: brake
* **X / Left Shift**: turbo
* **Enter**: confirm / pause
* **Esc / Backspace**: back (close the window to quit)
* **F12**: screenshot

Save data goes to `car_game_save.bin` next to the executable.

## Headless renderer

```bash
./car_game_headless <outdir> <frames> [options] [frame numbers to save...]
```

| Option | Meaning |
| --- | --- |
| `--race C` | skip title/menus and go straight to circuit C's grid + countdown |
| `--nocd` | with `--race` / `--gp`: skip the countdown (GO at frame 1) |
| `--gp` | start a Grand Prix from the first circuit (auto-confirms results/standings) |
| `--auto` | the autopilot drives the player car for the whole run |
| `--diff D` | difficulty 0/1/2 |
| `--cycle C` | override the day cycle (0 day, 1 sunset, 2 night, 3 dawn) and freeze it |
| `--pos Z` | teleport the player (camera) to track position Z right after GO |
| `--in A:B:SPEC` | input from frame A to B; SPEC = comma list of `L`,`R`,`U`,`D`,`steer=V`,`brake`,`turbo`,`start`,`confirm`,`back` |
| `--dump N` | every N frames print the state, raceTime and z/x/speed/place/lap of every racer |
| `--every N` | save every Nth frame |
| `--title` | no automatic presses (the title falls into the demo after 10 s idle) |
| `--save` | enable save data (`car_game_save.bin` in the current folder) |
| `--segs F` | at frame F print the 40 segments ahead of the camera |
| `--steer A:B:V`, `--brake A:B` | older forms of `--in` |

Without `--race` / `--gp` / `--title` the harness walks title → menu → QUICK
RACE → circuit 0 (presses at frames 30, 50, 60, 80). Frames are written as
`frame_NNNN.ppm`; the run ends with the state, the classification (when a race
ran) and the average host time per frame. It runs on a virtual 30 Hz clock and
everything is seeded, so runs are reproducible.

Contact sheets: `python tools/sheet.py <out.png> <scale> a.ppm b.ppm ...`
(2 columns, needs Pillow).

## How it works

* `Arduino.h/cpp`: Arduino API mock (`millis`, `random`, ...), plus the
  `platform.h` allocation/logging/save helpers. Headless runs use a virtual clock.
* `host_io.h/cpp`: `display_present()` copies the frame for the frontend,
  `input_read()` returns what the frontend set.
* `car_game_wrapper.cpp`: includes `../car_game.ino` so it compiles as C++.
* `main.cpp` / `main_headless.cpp`: the two frontends.

## Troubleshooting

### "g++: command not found"

Install a compiler: the Raylib Windows installer can include **w64devkit**
(MinGW), or download w64devkit from GitHub and add its `bin` folder to `PATH`.
