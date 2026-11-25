/*
  ═══════════════════════════════════════════════════════════════
  PLAYER INPUT
  The original game configured the two buttons but never read them
  (handleInput was autopilot only). Steering is real now; when nobody
  touches the controls for AUTOPILOT_IDLE_MS the autopilot takes over
  (attract/demo mode) and hands control back on the next input.
  ═══════════════════════════════════════════════════════════════
*/

#ifndef INPUT_H
#define INPUT_H

struct InputState {
  float steer;    // -1 (left) .. +1 (right)
  bool  brake;
  bool  start;    // start / confirm
  bool  any;      // any control touched this frame
};

void input_init();
InputState input_read();

#endif // INPUT_H
