/*
  ═══════════════════════════════════════════════════════════════
  PLATFORM ABSTRACTION
  The game core is target-neutral: it draws into a plain RGB565
  framebuffer (gfx.cpp) and hands finished frames to display.h.
  Only display_*.cpp / input_*.cpp know about the hardware.
  ═══════════════════════════════════════════════════════════════
*/

#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>
#include <stddef.h>

#if defined(ARDUINO_ARCH_ESP32)
  #include <esp_attr.h>
  #define PLATFORM_ESP32 1
  // Per-pixel loops must not run from flash: executing XIP through the
  // instruction cache while the same cache serves PSRAM and texture reads
  // thrashes it (lesson from the SOTN/MGS ports on this same chip).
  // noinline: GCC otherwise inlines a HOT function into its caller, which
  // lives in flash, and the IRAM placement is silently lost (measured:
  // rasterTri and convertBand ran from flash). So HOT goes ONLY on
  // functions that contain a whole per-pixel / per-span / per-column loop;
  // per-pixel helpers stay `static inline` without HOT.
  #define HOT IRAM_ATTR __attribute__((noinline))
  // Small hot tables kept in internal DRAM instead of flash-mapped rodata.
  #define FAST_DATA DRAM_ATTR
#else
  #define PLATFORM_ESP32 0
  #define HOT
  #define FAST_DATA
#endif

// Allocation helpers. On the ESP32-S3:
//   internal SRAM : ~1 cycle access, DMA-capable, scarce (~300 KB free)
//   PSRAM (octal) : 8 MB, goes through the shared 32/64 KB cache
// The render target lives in internal SRAM when it fits; big, rarely-touched
// buffers (scanout snapshots) go to PSRAM.
void* plat_alloc_internal(size_t bytes);
void* plat_alloc_psram(size_t bytes);
void  plat_log(const char* fmt, ...);
void  plat_report_memory(const char* tag);

// Persistent storage of one small blob (save data).
//   ESP32   : Preferences / NVS (namespace "arcade3d", key "save")
//   PC      : a small file next to the binary
//   headless: disabled unless the harness passes --save
// plat_load returns false when nothing (or a blob of another size) is stored.
bool  plat_save(const void* data, size_t n);
bool  plat_load(void* data, size_t n);

// ---- additions (UI phase) ----
// Once-per-second health line on the serial port (ESP32 only):
//   [st] fps, frame ms, LCD fps | internal free / largest / minimum ever |
//   PSRAM free | stack high-water of the loop task and of lcd_scan
// Set to 0 to silence it.
#ifndef PLAT_STATUS_LOG
#define PLAT_STATUS_LOG 1
#endif
// Early platform setup: logs every failed heap allocation (ESP32).
void  plat_init();
// Call once per second from the game loop (prints only if PLAT_STATUS_LOG).
void  plat_status(float fps, float frameMs, float lcdFps);

// ---- additions (integration) ----
// Boot check that a HOT loop really landed in IRAM (ESP32: logs
// "iram: <name> yes" or "iram: <name> NO (flash)"; nothing on the PC).
void  plat_log_iram(const char* name, const void* fn);

#endif // PLATFORM_H
