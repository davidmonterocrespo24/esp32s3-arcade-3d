/*
  ═══════════════════════════════════════════════════════════════
  RIVAL AI
  Each rival follows a precomputed racing line (outside before a
  corner, apex inside, back out) plus its own lateral preference, takes
  its speed from a per-segment corner/braking profile and its own pace,
  keeps a gap to the car ahead, overtakes on the side with room and
  never moves into a car alongside. No drift physics: the lateral
  motion is a limited-speed, limited-acceleration controller.
  The player's autopilot runs the same controller (ai_drivePlayer).
  See wiki/11-rivals-ai.md.
  ═══════════════════════════════════════════════════════════════
*/

#ifndef RIVALS_H
#define RIVALS_H

void rivals_init();            // per-rival skill, line preferences, start reaction
void rivals_update(float dt);  // drive the rivals (idle until race_go)

// Shared driving helpers (the player's autopilot uses them too)
// Racing-line x (road half-widths) at track position z, plus a lateral bias.
float ai_lineTarget(float z, float bias);
// Fraction of maxSpeed the corners just ahead of z allow (0.6 .. 1).
float ai_cornerSpeed(float z);

// ═══════════════════════════════════════════════════════════════
//  ADDITIONS (feature phase)
// ═══════════════════════════════════════════════════════════════
// What the controller wants a car to do this frame.
struct AiCmd {
  float xTarget;     // lateral target (road half-widths)
  float xVel;        // lateral velocity to follow (half-widths / s), already
                     // limited in speed and acceleration
  float vTarget;     // speed limit of the corners / top speed (world units / s):
                     // brake when above it by more than AI_CORNER_TOL (like a human)
  float vTraffic;    // speed that keeps the gap to the car ahead: ease off above it
  bool  brake;       // press the brake
  bool  turbo;       // press the turbo
};
#define AI_CORNER_TOL 330.0f   // world units / s over the corner profile before braking

// The autopilot: the rival controller driving the player's car (demo,
// finish cool-down, --auto). Called by handleInput() every frame while
// `autopilot` is set; racers[PLAYER_ID] must be in sync (race_syncPlayer).
void ai_drivePlayer(float dt, AiCmd& out);

// Lateral velocity of racer i's controller (half-widths / s). Contacts
// kick it (physics_resolveContacts) so bumped rivals bounce visibly.
float ai_latVel(int i);
void  ai_kickLatVel(int i, float dv);

// Rival turbo state for the renderer (flame) and the HUD: meter 0..1.
float ai_turboMeter(int i);

// Personality of racer i (for the UI, optional): one word, upper case.
const char* ai_style(int i);

#endif // RIVALS_H
