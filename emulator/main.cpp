// Raylib frontend: runs the game's setup()/loop() and shows the RGB565
// framebuffer it presents, scaled x3. The game draws into exactly the same
// software framebuffer as on the ESP32, so what you see here is pixel-exact.
//
// Keys: LEFT/RIGHT steer (and change values / circuits in menus), UP/DOWN
// menus, SPACE or DOWN brake, X or LEFT SHIFT turbo, ENTER confirm / pause,
// P pause / resume, ESC or BACKSPACE back, F12 screenshot.
// Close the window to quit (ESC is "back" in the game).

#include <string>
#include "raylib.h"
#include "Arduino.h"
#include "host_io.h"

extern void setup();
extern void loop();

int main(int argc, char** argv) {
  // Save data next to the binary
  if (argc > 0 && argv[0]) {
    std::string p = argv[0];
    size_t s = p.find_last_of("/\\");
    g_emuSavePath = (s == std::string::npos ? std::string() : p.substr(0, s + 1)) + "car_game_save.bin";
  }

  const int SCALE = 3;
  InitWindow(SCR_W * SCALE, SCR_H * SCALE, "ESP32-S3 Arcade 3D - emulator");
  SetExitKey(KEY_NULL);
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
    InputState in = { 0, 0, false, false, false, false, false, false };
    if (IsKeyDown(KEY_LEFT))  in.steer -= 1.0f;
    if (IsKeyDown(KEY_RIGHT)) in.steer += 1.0f;
    if (IsKeyDown(KEY_UP))    in.stickY -= 1.0f;
    if (IsKeyDown(KEY_DOWN))  in.stickY += 1.0f;
    in.brake = IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_SPACE);
    in.turbo = IsKeyDown(KEY_X) || IsKeyDown(KEY_LEFT_SHIFT);
    in.confirm = IsKeyDown(KEY_ENTER) || IsKeyDown(KEY_KP_ENTER);
    in.start = in.confirm || IsKeyDown(KEY_P);    // P pauses / resumes without confirming
    in.back = IsKeyDown(KEY_ESCAPE) || IsKeyDown(KEY_BACKSPACE);
    in.any = in.steer != 0 || in.stickY != 0 || in.brake || in.turbo || in.start || in.confirm || in.back;
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
