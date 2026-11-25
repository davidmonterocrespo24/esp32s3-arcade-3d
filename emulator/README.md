# PC builds of the ESP32 Car Game

The game draws into its own RGB565 software framebuffer (`../gfx.cpp`) on every
platform, so the PC builds show exactly the pixels the ESP32 sends to the panel.
Only the frontends differ:

| Target | Command | Needs |
| ------ | ------- | ----- |
| `car_game_emu.exe` | `make` | MinGW g++ + [Raylib](https://www.raylib.com/) (`RAYLIB_PATH`, default `C:/raylib`) |
| `car_game_headless` | `make headless` | any C++17 g++ (Linux/WSL/MinGW) |

## Emulator controls

* **Left / Right**: steer
* **Down / Space**: brake
* **Enter**: start
* **F12**: screenshot

## Headless renderer

```bash
./car_game_headless <outdir> <frames> [options] [frame numbers to save...]
```

Options: `--cycle C` (0 day, 1 sunset, 2 night, 3 dawn), `--pos Z` (teleport),
`--steer A:B:V`, `--brake A:B`, `--title` (stay on the title screen).
Frames are written as `frame_NNNN.ppm` and the average host time per frame is
printed at the end.

## How it works

* `Arduino.h/cpp`: Arduino API mock (`millis`, `random`, ...), plus the
  `platform.h` allocation/logging helpers. Headless runs use a virtual clock.
* `host_io.h/cpp`: `display_present()` copies the frame for the frontend,
  `input_read()` returns what the frontend set.
* `car_game_wrapper.cpp`: includes `../car_game.ino` so it compiles as C++.
* `main.cpp` / `main_headless.cpp`: the two frontends.

## Troubleshooting

### "g++: command not found"

Install a compiler: the Raylib Windows installer can include **w64devkit**
(MinGW), or download w64devkit from GitHub and add its `bin` folder to `PATH`.
