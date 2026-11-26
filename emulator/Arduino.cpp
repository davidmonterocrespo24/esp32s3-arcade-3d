// Arduino API + platform helpers for the PC builds. No Raylib in here, so
// the same file serves the windowed emulator and the headless renderer.

#include <chrono>
#include <thread>
#include <random>
#include <stdarg.h>
#include "Arduino.h"
#include "../platform.h"

SerialMock Serial;

bool          g_emuVirtualClock = false;
unsigned long g_emuNowMs = 0;

static auto start_time = std::chrono::steady_clock::now();
static std::mt19937 rng(12345);

unsigned long millis() {
  if (g_emuVirtualClock) return g_emuNowMs;
  auto now = std::chrono::steady_clock::now();
  return (unsigned long)std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
}

unsigned long micros() {
  if (g_emuVirtualClock) return g_emuNowMs * 1000UL;
  auto now = std::chrono::steady_clock::now();
  return (unsigned long)std::chrono::duration_cast<std::chrono::microseconds>(now - start_time).count();
}

void delay(unsigned long ms) {
  if (g_emuVirtualClock) { g_emuNowMs += ms; return; }
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void randomSeed(unsigned long seed) {
  // Headless runs keep their fixed seed so screenshots are reproducible
  if (!g_emuVirtualClock) rng.seed((uint32_t)seed);
}

long random(long howbig) {
  if (howbig <= 0) return 0;
  return (long)(rng() % (uint32_t)howbig);
}

long random(long lo, long hi) {
  if (lo >= hi) return lo;
  return lo + random(hi - lo);
}

int analogRead(uint8_t) { return 0; }
void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t, uint8_t) {}
int digitalRead(uint8_t) { return HIGH; }

// ---- platform.h -----------------------------------------------------------
void* plat_alloc_internal(size_t bytes) { return calloc(1, bytes); }
void* plat_alloc_psram(size_t bytes) { return calloc(1, bytes); }

void plat_log(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
}

void plat_report_memory(const char*) {}

bool        g_emuSaveEnabled = true;
std::string g_emuSavePath = "car_game_save.bin";

bool plat_save(const void* data, size_t n) {
  if (!g_emuSaveEnabled) return false;
  FILE* f = fopen(g_emuSavePath.c_str(), "wb");
  if (!f) return false;
  bool ok = fwrite(data, 1, n, f) == n;
  fclose(f);
  return ok;
}

bool plat_load(void* data, size_t n) {
  if (!g_emuSaveEnabled) return false;
  FILE* f = fopen(g_emuSavePath.c_str(), "rb");
  if (!f) return false;
  bool ok = fread(data, 1, n, f) == n && fgetc(f) == EOF;   // exact size only
  fclose(f);
  return ok;
}
