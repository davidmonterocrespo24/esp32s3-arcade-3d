/*
  ═══════════════════════════════════════════════════════════════
  3D CAR RENDERING (player + traffic share the same mesh renderer)
  ═══════════════════════════════════════════════════════════════
*/

#ifndef RENDER_PLAYER_H
#define RENDER_PLAYER_H

#include <stdint.h>

struct CarRender {
  float ax, ay;          // screen anchor: the car's ground centre
  float pxPerUnit;       // screen pixels per model unit at the anchor
  float yaw;             // + turns the nose right
  float pitch;           // camera elevation + road slope (radians)
  float roll;            // body lean (radians, + leans right)
  float camDist;         // eye -> anchor distance in model units (perspective)
  uint16_t paint;        // 0 = texture paint, else RGB565 paint colour
  float fog;             // 0..1 blend towards fogColor
  uint16_t fogColor;
  float lightScale;      // global light multiplier (tunnels)
  bool  tunnelLight;     // light from the tunnel ceiling instead of the sun
  bool  lights;          // tail lights on
  bool  brake;           // brake lights (brighter)
  bool  shadow;          // draw the contact shadow
};

// Copy the texture to internal RAM and tag its paint texels (call once).
void initCarRenderer();

// Draw one car with the current palette lighting.
void renderCar(const CarRender& cr);

// Player car, start screen, crash overlay
void drawPlayerCar(float timeSec);
void drawStartScreen(float timeSec);
void drawCrashMessage(float sinceCrashSec);

#endif // RENDER_PLAYER_H
