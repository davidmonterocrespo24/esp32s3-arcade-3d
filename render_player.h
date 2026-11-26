/*
  ═══════════════════════════════════════════════════════════════
  3D CAR RENDERING (player and rivals share the same mesh renderer)
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

// Viewpoint the player's car is drawn from (rivals near the camera use it too)
#define PLAYER_VIEW_PITCH 0.30f   // radians
#define PLAYER_VIEW_DIST  11.0f   // eye distance in model units
#define PLAYER_PX_PER_UNIT 27.0f

// Copy the texture to internal RAM and tag its paint texels (call once).
void initCarRenderer();

// Draw one car with the current palette lighting.
void renderCar(const CarRender& cr);

// The player's car at its fixed screen position (after drawRoad)
void drawPlayerCar(float timeSec);

#endif // RENDER_PLAYER_H
