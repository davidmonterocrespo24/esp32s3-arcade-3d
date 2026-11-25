// Raylib frontend: runs the game's setup()/loop() and shows the RGB565
// framebuffer it presents, scaled x3. The game draws into exactly the same
// software framebuffer as on the ESP32, so what you see here is pixel-exact.
//
// Keys: LEFT/RIGHT steer, DOWN or SPACE brake, ENTER start, F12 screenshot.

#include "raylib.h"
#include "Arduino.h"
#include "host_io.h"

extern void setup();
extern void loop();

int main() {
  const int SCALE = 3;
  InitWindow(SCR_W * SCALE, SCR_H * SCALE, "ESP32-S3 Arcade 3D - emulator");
  SetTargetFPS(60);

  Image img;
  img.data = g_hostFrame;
  img.width = SCR_W;
  img.height = SCR_H;
  img.mipmaps = 1;
  img.format = PIXELFORMAT_UNCOMPRESSED_R5G6B5;
  Texture2D tex = LoadTextureFromImage(img);
  SetTextureFilter(tex, TEXTURE_FILTER_POINT);

  setup();

  while (!WindowShouldClose()) {
    InputState in = { 0, false, false, false };
    if (IsKeyDown(KEY_LEFT))  in.steer -= 1.0f;
    if (IsKeyDown(KEY_RIGHT)) in.steer += 1.0f;
    in.brake = IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_SPACE);
    in.start = IsKeyDown(KEY_ENTER);
    in.any = in.steer != 0 || in.brake || in.start;
    g_hostInput = in;

    loop();

    if (g_hostFrameNew) {
      UpdateTexture(tex, g_hostFrame);
      g_hostFrameNew = false;
    }
    BeginDrawing();
    ClearBackground(BLACK);
    DrawTextureEx(tex, Vector2{ 0, 0 }, 0.0f, (float)SCALE, WHITE);
    EndDrawing();

    if (IsKeyPressed(KEY_F12)) TakeScreenshot("screenshot.png");
  }

  UnloadTexture(tex);
  CloseWindow();
  return 0;
}
