/*
  ═══════════════════════════════════════════════════════════════
  ESP32 INPUT (board-specific)
  XIAO handheld : analog stick (X steers, Y navigates menus), A (D5)
                  brakes, B (D7) turbo / confirm, six buttons on a
                  resistor ladder (D2). The stick calibrates its centre
                  at boot (a drone gimbal does not rest at mid-scale) and
                  learns its travel while used — don't touch it while
                  powering on.
  DEVKIT        : two buttons, left / right; both together = confirm /
                  pause.
  ═══════════════════════════════════════════════════════════════
*/

#if defined(ARDUINO_ARCH_ESP32)

#include <Arduino.h>
#include "input.h"
#include "board.h"
#include "config.h"

#if BOARD == BOARD_XIAO_HANDHELD

struct Axis { int centre, lo, hi; };
static Axis sAxX = { 2048, 2048, 2048 }, sAxY = { 2048, 2048, 2048 };

static int readAvg(int pin, int n) {
  int s = 0;
  for (int i = 0; i < n; i++) s += analogRead(pin);
  return s / n;
}

static void calibrate(Axis& a, int pin) {
  a.centre = readAvg(pin, 16);
  a.lo = a.hi = a.centre;
}

// raw counts -> -1..+1 with the dead zone subtracted (motion starts from 0)
static float axisValue(Axis& a, int raw) {
  if (raw < a.lo) a.lo = raw;             // learn the real travel as it is used
  if (raw > a.hi) a.hi = raw;
  int d = raw - a.centre;
  int span = (d >= 0) ? (a.hi - a.centre) : (a.centre - a.lo);
  if (span < 500) span = 500;             // before the stick has been swept
  int m = d < 0 ? -d : d;
  if (m <= STICK_DEADZONE) return 0.0f;
  float v = (float)(m - STICK_DEADZONE) / (float)(span - STICK_DEADZONE);
  if (v > 1) v = 1;
  return d < 0 ? -v : v;
}

void input_init() {
  analogReadResolution(12);
  pinMode(PIN_BTN_A, INPUT_PULLUP);
  pinMode(PIN_BTN_B, INPUT_PULLUP);
  calibrate(sAxX, PIN_STICK_X);
  calibrate(sAxY, PIN_STICK_Y);
}

InputState input_read() {
  InputState in = { 0, 0, false, false, false, false, false, false };
  float x = axisValue(sAxX, readAvg(PIN_STICK_X, 4));
  float y = axisValue(sAxY, readAvg(PIN_STICK_Y, 4));
  in.steer = STICK_INVERT_X ? -x : x;
  // Raw Y grows when the stick is pushed UP on this unit (STICK_INVERT_Y 0);
  // InputState wants -1 = up.
  in.stickY = STICK_INVERT_Y ? y : -y;

  const bool a = digitalRead(PIN_BTN_A) == LOW;
  const bool b = digitalRead(PIN_BTN_B) == LOW;
  // The ladder reports only the lowest-resistance button held
  const int lad = analogRead(PIN_BTN_LADDER);
  const bool circle   = lad < LADDER_W1_MAX;
  const bool cross    = !circle && lad < LADDER_W2_MAX;
  const bool startKey = lad >= LADDER_W4_MAX && lad < LADDER_W5_MAX;
  const bool selectKey = lad >= LADDER_W5_MAX && lad < LADDER_W6_MAX;

  in.brake   = a;
  in.turbo   = b;
  in.start   = startKey;
  in.confirm = startKey || b || cross;
  in.back    = selectKey || circle;
  in.any = in.steer != 0 || in.stickY != 0 || a || b || lad < LADDER_W6_MAX;
  return in;
}

#elif BOARD == BOARD_DEVKIT

void input_init() {
  pinMode(PIN_BTN_LEFT, INPUT_PULLUP);
  pinMode(PIN_BTN_RIGHT, INPUT_PULLUP);
}

InputState input_read() {
  InputState in = { 0, 0, false, false, false, false, false, false };
  bool l = digitalRead(PIN_BTN_LEFT) == LOW;
  bool r = digitalRead(PIN_BTN_RIGHT) == LOW;
  bool both = l && r;
  in.steer = both ? 0.0f : (r ? 1.0f : 0.0f) - (l ? 1.0f : 0.0f);
  in.start = both;
  in.confirm = both;
  in.any = l || r;
  return in;
}

#endif

#endif // ARDUINO_ARCH_ESP32
