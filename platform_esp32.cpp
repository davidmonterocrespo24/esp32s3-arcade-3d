/*
  ═══════════════════════════════════════════════════════════════
  ESP32 PLATFORM HELPERS (memory placement + logging)
  ═══════════════════════════════════════════════════════════════
*/

#if defined(ARDUINO_ARCH_ESP32)

#include <Arduino.h>
#include <Preferences.h>
#include <stdarg.h>
#include <stdio.h>
#include "esp_heap_caps.h"
#include "platform.h"

void* plat_alloc_internal(size_t bytes) {
  return heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void* plat_alloc_psram(size_t bytes) {
  return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void plat_log(const char* fmt, ...) {
  char buf[200];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.print(buf);
}

void plat_report_memory(const char* tag) {
  plat_log("[mem] %-10s internal free %6u (largest %6u) | psram free %7u\n", tag,
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

// ---- Save data in NVS (a few ms per write: only call it between races) ----
bool plat_save(const void* data, size_t n) {
  Preferences prefs;
  if (!prefs.begin("arcade3d", false)) return false;
  size_t w = prefs.putBytes("save", data, n);
  prefs.end();
  return w == n;
}

bool plat_load(void* data, size_t n) {
  Preferences prefs;
  // read-only open fails on the very first boot (namespace not created yet)
  if (!prefs.begin("arcade3d", true)) return false;
  bool ok = prefs.getBytesLength("save") == n && prefs.getBytes("save", data, n) == n;
  prefs.end();
  return ok;
}

#endif // ARDUINO_ARCH_ESP32
