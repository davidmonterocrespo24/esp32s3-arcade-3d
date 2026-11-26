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
  #define HOT IRAM_ATTR
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

#endif // PLATFORM_H
