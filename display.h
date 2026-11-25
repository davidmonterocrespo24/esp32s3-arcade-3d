/*
  ═══════════════════════════════════════════════════════════════
  DISPLAY OUTPUT
  display_present() hands a finished framebuffer to the scanout.
  On the ESP32 the panel is fed by SPI DMA from the other core; a
  frame the panel cannot take yet is dropped (the game never waits
  for the wire). On the PC the frame is uploaded to a window/file.
  ═══════════════════════════════════════════════════════════════
*/

#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

struct DisplayStats {
  uint32_t offered;       // frames handed to display_present()
  uint32_t dropped;       // frames skipped because the panel was busy
  uint32_t shown;         // frames fully sent to the panel
  uint32_t bandsSkipped;  // 8-line bands not sent (identical to last frame)
  uint32_t timeouts;      // lost DMA completions (recovered)
  uint32_t sendFails;     // refused SPI submissions (recovered)
  uint32_t lastSendUs;    // wire time of the last frame
  uint32_t copyUs;        // time the game spent copying its frame
};

extern DisplayStats displayStats;

bool display_init();
void display_present(const uint16_t* fb);

#endif // DISPLAY_H
