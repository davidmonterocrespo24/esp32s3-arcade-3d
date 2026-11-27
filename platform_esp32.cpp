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
#include "esp_memory_utils.h"
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

// ---- Failed allocations and the once-per-second status line ----
// The callback can run in any task: it only formats into a small stack
// buffer and prints (the USB CDC TX timeout is 0, so it never blocks).
static void failedAlloc(size_t size, uint32_t caps, const char* fn) {
  char buf[96];
  snprintf(buf, sizeof(buf), "[mem] ALLOC FAILED %u B caps 0x%x in %s\n", (unsigned)size, (unsigned)caps,
           fn ? fn : "?");
  Serial.print(buf);
}

void plat_init() {
  heap_caps_register_failed_alloc_callback(failedAlloc);
}

void plat_log_iram(const char* name, const void* fn) {
  plat_log("iram: %-18s %s\n", name, esp_ptr_in_iram(fn) ? "yes" : "NO (flash)");
}

void plat_status(float fps, float frameMs, float lcdFps) {
#if PLAT_STATUS_LOG
  static TaskHandle_t sLcd = nullptr, sAdc = nullptr;
  if (!sLcd) sLcd = xTaskGetHandle("lcd_scan");
  if (!sAdc) sAdc = xTaskGetHandle("input_adc");
  unsigned loopHW = (unsigned)uxTaskGetStackHighWaterMark(nullptr);
  unsigned lcdHW = sLcd ? (unsigned)uxTaskGetStackHighWaterMark(sLcd) : 0u;
  unsigned adcHW = sAdc ? (unsigned)uxTaskGetStackHighWaterMark(sAdc) : 0u;
  plat_log("[st] %4.1f fps %5.1f ms lcd %4.1f | int %6u lg %6u min %6u | psram %7u | stk loop %u lcd %u adc %u\n",
           fps, frameMs, lcdFps,
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), loopHW, lcdHW, adcHW);
#else
  (void)fps; (void)frameMs; (void)lcdFps;
#endif
}

#endif // ARDUINO_ARCH_ESP32
