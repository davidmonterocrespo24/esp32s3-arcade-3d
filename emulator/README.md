# ESP32 Car Game Emulator (Windows)

This folder contains a Windows wrapper for the ESP32 Car Game, allowing you to run and debug the game on your PC using [Raylib](https://www.raylib.com/).

## Prerequisites

1.  **MinGW / G++**: You need a C++ compiler. You can use the one bundled with Raylib or install MinGW-w64.
2.  **Raylib**: Download and install Raylib for Windows.
    *   Recommendation: Use the [Raylib Installer](https://github.com/raysan5/raylib/releases) which installs to `C:/raylib`.

## Configuration

Open `Makefile` and check the `RAYLIB_PATH` variable.
Default is:
```makefile
RAYLIB_PATH ?= C:/raylib/raylib
```
If you installed Raylib elsewhere, update this path or pass it when running make.

## Building

Open a terminal in this `emulator/` directory and run:

```cmd
mingw32-make
```
(Or just `make` if you have it aliased).

## Running

Run the generated executable:
```cmd
car_game_emu.exe
```

## Controls

*   **Left Arrow**: Steer Left (Simulates BTN_LEFT)
*   **Right Arrow**: Steer Right (Simulates BTN_RIGHT)

## How it Works

*   `Arduino.h/cpp`: Mocks the Arduino API (`millis`, `delay`, `digitalRead`, etc.).
*   `TFT_eSPI.h/cpp`: Mocks the TFT library using Raylib for rendering.
*   `car_game_wrapper.cpp`: Includes the original `car_game.ino` file to compile the game logic as part of the C++ application.
*   `main.cpp`: The Windows entry point that initializes the window and runs the game loop.

## Troubleshooting

## Troubleshooting

## Troubleshooting

### "g++: command not found" or "Make: *** [main.o] Error"
This means you are missing the C++ compiler.

**Option A: The Easy Way (Installer)**
1.  Go to [itch.io Raylib](https://raylib.itch.io/raylib) and download the **Windows Installer**.
2.  Run it and check **"Install w64devkit (MinGW compiler)"**.

**Option B: The Manual Way (w64devkit)**
1.  Download **w64devkit** from [GitHub](https://github.com/skeeto/w64devkit/releases).
2.  Extract it to a folder (e.g., `C:\w64devkit`).
3.  Add `C:\w64devkit\bin` to your **System Environment Variables (PATH)**.
4.  Restart your terminal.

