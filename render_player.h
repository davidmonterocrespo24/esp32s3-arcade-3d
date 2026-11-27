/*
  ═══════════════════════════════════════════════════════════════
  3D CAR RENDERING (player and rivals share the same mesh renderer)
  ═══════════════════════════════════════════════════════════════
*/

#ifndef RENDER_PLAYER_H
#define RENDER_PLAYER_H

#include <stdint.h>
#include "config.h"

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
  float wheelSteer = 0;  // front wheels turned against the body (radians, + = right)
  float bodyRoll = 0;    // body lean on its springs (radians, + leans right); the wheels stay upright
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

// ═══════════════════════════════════════════════════════════════
//  ADDITIONS (feature phase)
// ═══════════════════════════════════════════════════════════════
// Screen anchor of the player's car: its ground centre is drawn here
// (bounce excluded). Rivals near the player use the same "car camera".
#define PLAYER_ANCHOR_Y   (SCR_H - 13)
#define PLAYER_STEER_PX   6.0f        // anchor x shift per unit of steerVisual

// Steering look: the front wheels turn and the body leans out of the turn on
// soft springs (it lags the steering and overshoots a little on release).
#define CAR_WHEEL_STEER    0.45f      // front-wheel angle at full steering (radians)
#define CAR_BODY_ROLL      0.08f      // body lean at full steering and full speed (radians)
#define BODY_ROLL_FREQ     8.0f       // suspension natural frequency (rad/s)
#define BODY_ROLL_DAMPING  0.4f       // suspension damping ratio (< 1 overshoots)

// Level of detail for renderCarLod (render_cars picks it with hysteresis)
enum CarLod : uint8_t {
  CAR_LOD_FULL = 0,   // Gouraud diffuse + specular + sky reflection, two shadow rings
  CAR_LOD_MID,        // diffuse + ambient only, one shadow ring (small cars)
};

// renderCar() at a chosen level of detail (renderCar == CAR_LOD_FULL).
void renderCarLod(const CarRender& cr, int lod);

// Turbo flame from the two exhausts of a car drawn with `cr` (call right
// after rendering it). strength 0..1 (e.g. fading in/out), time for flicker.
void renderCarFlame(const CarRender& cr, float strength, float timeSec);

// The player's road-slope pitch as drawn this frame (radians, smoothed),
// so rivals beside the player can be pitched the same way.
float playerViewSlope();

#endif // RENDER_PLAYER_H
