/*
  ═══════════════════════════════════════════════════════════════
  ESP32 INPUT (board-specific)
  XIAO handheld : analog stick X steers, D5 brakes, D7 / ladder START
                  starts. Stick calibrates its centre at boot (a drone
                  gimbal does not rest at mid-scale) — don't touch it
                  while powering on.
  DEVKIT        : two buttons, left / right.
  ═══════════════════════════════════════════════════════════════
*/

#if defined(ARDUINO_ARCH_ESP32)

#include <Arduino.h>
#include "input.h"
#include "board.h"
#include "config.h"

#if BOARD == BOARD_XIAO_HANDHELD

static int sCentre = 2048, sLo = 2048, sHi = 2048;

static int readAvg(int pin, int n) {
  int s = 0;
  for (int i = 0; i < n; i++) s += analogRead(pin);
  return s / n;
}

void input_init() {
  analogReadResolution(12);
  pinMode(PIN_BTN_A, INPUT_PULLUP);
  pinMode(PIN_BTN_B, INPUT_PULLUP);
  sCentre = readAvg(PIN_STICK_X, 16);
  sLo = sHi = sCentre;
}

InputState input_read() {
  InputState in = { 0, false, false, false };
  int raw = readAvg(PIN_STICK_X, 4);
  if (raw < sLo) sLo = raw;             // learn the real travel as it is used
  if (raw > sHi) sHi = raw;
  int d = raw - sCentre;
  int span = (d >= 0) ? (sHi - sCentre) : (sCentre - sLo);
  if (span < 500) span = 500;           // before the stick has been swept
  int a = d < 0 ? -d : d;
  if (a > STICK_DEADZONE) {
    float v = (float)(a - STICK_DEADZONE) / (float)(span - STICK_DEADZONE);
    if (v > 1) v = 1;
    in.steer = (d < 0) ? -v : v;
    if (STICK_INVERT_X) in.steer = -in.steer;
  }
  in.brake = digitalRead(PIN_BTN_A) == LOW;
  bool b = digitalRead(PIN_BTN_B) == LOW;
  int ladder = analogRead(PIN_BTN_LADDER);
  bool startKey = ladder > LADDER_W4_MAX && ladder <= LADDER_W5_MAX;
  in.start = b || startKey;
  in.any = in.steer != 0 || in.brake || in.start || ladder < LADDER_W6_MAX;
  return in;
}

#elif BOARD == BOARD_DEVKIT

void input_init() {
  pinMode(PIN_BTN_LEFT, INPUT_PULLUP);
  pinMode(PIN_BTN_RIGHT, INPUT_PULLUP);
}

InputState input_read() {
  InputState in = { 0, false, false, false };
  bool l = digitalRead(PIN_BTN_LEFT) == LOW;
  bool r = digitalRead(PIN_BTN_RIGHT) == LOW;
  in.steer = (r ? 1.0f : 0.0f) - (l ? 1.0f : 0.0f);
  in.start = l && r;
  in.any = l || r;
  return in;
}

#endif

#endif // ARDUINO_ARCH_ESP32
