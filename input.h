/*
  ═══════════════════════════════════════════════════════════════
  PLAYER INPUT (one snapshot per frame; edges are derived by the
  game from consecutive snapshots)

  | Action  | XIAO handheld            | Dev board       | PC emulator        |
  | Steer   | stick X                  | GPIO17 / GPIO16 | Left / Right       |
  | Menu    | stick Y                  | (steer)         | Up / Down          |
  | Brake   | A (D5)                   | -               | Space or Down      |
  | Turbo   | B (D7)                   | -               | X or Left Shift    |
  | Confirm | START, B or CROSS        | both buttons    | Enter              |
  | Back    | SELECT or CIRCLE         | -               | Esc / Backspace    |
  | Pause   | START                    | both buttons    | Enter              |
  ═══════════════════════════════════════════════════════════════
*/

#ifndef INPUT_H
#define INPUT_H

struct InputState {
  float steer;     // -1 .. +1
  float stickY;    // -1 up .. +1 down (menus)
  bool  brake, turbo;
  bool  start;     // pause / skip
  bool  confirm;   // menu accept
  bool  back;      // menu back
  bool  any;       // any control touched
};

void input_init();
InputState input_read();

#endif // INPUT_H
