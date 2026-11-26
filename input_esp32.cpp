/*
  ═══════════════════════════════════════════════════════════════
  ESP32 INPUT (board-specific)
  XIAO handheld : analog stick (X steers, Y navigates menus), A (D5)
                  brakes, B (D7) turbo / confirm, six buttons on a
                  resistor ladder (D2). The stick calibrates its centre
                  at boot (a drone gimbal does not rest at mid-scale) and
                  learns its travel while used — don't touch it while
                  powering on.
                  The five ADC conversions (stick X and Y twice, the
                  ladder once) run in a small task on core 0 every 4 ms
                  and are published as volatile values, so the game loop
                  never waits for the ADC. The ladder window must read the
                  same twice in a row: while a button is released the
                  voltage sweeps through the windows of other buttons
                  (START -> SELECT would read as a "back" press).
  DEVKIT        : two buttons, left / right. Both together = confirm at
                  once (menus); START (pause) needs both held 250 ms, so a
                  quick left-right overlap while racing never pauses.
  ═══════════════════════════════════════════════════════════════
*/

#if defined(ARDUINO_ARCH_ESP32)

#include <Arduino.h>
#include "input.h"
#include "board.h"
#include "config.h"
#include "platform.h"

#if BOARD == BOARD_XIAO_HANDHELD

struct Axis { int centre, lo, hi; };
static Axis sAxX = { 2048, 2048, 2048 }, sAxY = { 2048, 2048, 2048 };

// Published by the ADC task
static volatile int sRawX = 2048, sRawY = 2048;
static volatile int sLadWin = 0;          // debounced ladder window, 0 = released
static bool sAdcTask = false;

static int readAvg(int pin, int n) {
  int s = 0;
  for (int i = 0; i < n; i++) s += analogRead(pin);
  return n > 0 ? s / n : 0;
}

static void calibrate(Axis& a, int pin) {
  a.centre = readAvg(pin, 16);
  a.lo = a.hi = a.centre;
}

// Ladder reading -> window: 1 CIRCLE, 2 CROSS, 3 TRIANGLE, 4 L1, 5 START,
// 6 SELECT, 0 released. Only the lowest-resistance button held is seen.
static int ladderWindow(int v) {
  if (v < LADDER_W1_MAX) return 1;
  if (v < LADDER_W2_MAX) return 2;
  if (v < LADDER_W3_MAX) return 3;
  if (v < LADDER_W4_MAX) return 4;
  if (v < LADDER_W5_MAX) return 5;
  if (v < LADDER_W6_MAX) return 6;
  return 0;
}

static void adcTask(void*) {
  int lastWin = 0;
  for (;;) {
    int x = analogRead(PIN_STICK_X) + analogRead(PIN_STICK_X);
    int y = analogRead(PIN_STICK_Y) + analogRead(PIN_STICK_Y);
    int w = ladderWindow(analogRead(PIN_BTN_LADDER));
    sRawX = x >> 1;
    sRawY = y >> 1;
    if (w == lastWin) sLadWin = w;
    lastWin = w;
    vTaskDelay(pdMS_TO_TICKS(4));
  }
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
  sRawX = sAxX.centre;
  sRawY = sAxY.centre;
  // core 0 (the LCD scanout task runs there at priority 5), 4 KB stack
  sAdcTask = xTaskCreatePinnedToCore(adcTask, "input_adc", 4096, nullptr, 3, nullptr, 0) == pdPASS;
  plat_log("input: stick centre x %d y %d, ADC %s\n", sAxX.centre, sAxY.centre,
           sAdcTask ? "task on core 0" : "read in the game loop (task creation failed)");
}

InputState input_read() {
  InputState in = { 0, 0, false, false, false, false, false, false };
  int rx, ry, win;
  if (sAdcTask) {
    rx = sRawX; ry = sRawY; win = sLadWin;
  } else {
    rx = readAvg(PIN_STICK_X, 2); ry = readAvg(PIN_STICK_Y, 2);
    win = ladderWindow(analogRead(PIN_BTN_LADDER));
  }
  float x = axisValue(sAxX, rx);
  float y = axisValue(sAxY, ry);
  in.steer = STICK_INVERT_X ? -x : x;
  // Raw Y grows when the stick is pushed UP on this unit (STICK_INVERT_Y 0);
  // InputState wants -1 = up.
  in.stickY = STICK_INVERT_Y ? y : -y;

  const bool a = digitalRead(PIN_BTN_A) == LOW;
  const bool b = digitalRead(PIN_BTN_B) == LOW;
  const bool circle = win == 1, cross = win == 2, startKey = win == 5, selectKey = win == 6;

  in.brake   = a;
  in.turbo   = b;
  in.start   = startKey;
  in.confirm = startKey || b || cross;
  in.back    = selectKey || circle;
  in.any = in.steer != 0 || in.stickY != 0 || a || b || win != 0;
  return in;
}

#elif BOARD == BOARD_DEVKIT

static uint32_t sBothSince = 0;

void input_init() {
  pinMode(PIN_BTN_LEFT, INPUT_PULLUP);
  pinMode(PIN_BTN_RIGHT, INPUT_PULLUP);
}

InputState input_read() {
  InputState in = { 0, 0, false, false, false, false, false, false };
  bool l = digitalRead(PIN_BTN_LEFT) == LOW;
  bool r = digitalRead(PIN_BTN_RIGHT) == LOW;
  bool both = l && r;
  uint32_t now = millis();
  if (!both) sBothSince = 0;
  else if (!sBothSince) sBothSince = now ? now : 1;
  in.steer = both ? 0.0f : (r ? 1.0f : 0.0f) - (l ? 1.0f : 0.0f);
  in.confirm = both;
  in.start = both && (now - sBothSince) >= 250;
  in.any = l || r;
  return in;
}

#endif

#endif // ARDUINO_ARCH_ESP32
