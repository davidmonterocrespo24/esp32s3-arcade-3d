#include "raylib.h"
#include "Arduino.h"
#include "TFT_eSPI.h"

// Externs from the game
extern void setup();
extern void loop();

int main() {
    // 1. Initialize Window
    InitWindow(320, 240, "ESP32 Car Game Emulator");
    SetTargetFPS(60);

    // 2. Setup (Game Logic)
    setup();

    // 3. Main Loop
    while (!WindowShouldClose()) {
        BeginDrawing();
        // ClearBackground(BLACK); // loop calls this via TFT calls usually, or we do it here?
        // The game loop calls spr.pushSprite(0,0) which draws to screen.
        // calls to spr.fillSprite/etc will draw to texture.
        
        loop(); // This calls drawSky, drawRoad -> spr.pushSprite -> DrawTextureRec
        
        // Debug info
        DrawFPS(10, 10);
        
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
