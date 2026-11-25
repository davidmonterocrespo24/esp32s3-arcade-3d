# 4. ESP32 display pipeline (taken from the MGS/SOTN ports)

`display_esp32.cpp` is a port of the scanout used in
`mgs_reversing/esp32/main/lcd_esp32.c` and `sotn-decomp/esp32/main/lcd_esp32.c`
on the same handheld, adapted to Arduino and to this game's framebuffer.

## 4.1 Flow

```
game task (core 1, Arduino loop)            scan task (core 0, prio 5)
────────────────────────────────            ──────────────────────────
render into gfx_fb (internal SRAM)
display_present(fb):
  queue not empty?  -> drop frame (count)
  memcpy fb -> snap[i] (PSRAM)   ──queue──>  for each of 30 bands (8 lines, 5 KB):
  return, keep rendering                       take a free DMA chunk (semaphore)
                                               byte-swap snap band -> chunk + hash
                                               hash == last frame? give back, skip
                                               else queue the chunk on SPI DMA
                                               (ISR gives the semaphore back)
```

- **Why a snapshot**: at 40 MHz a 320×240 frame takes ~31 ms on the wire. If
  the panel read the live framebuffer the game would lap it and the panel would
  receive half-drawn frames (the flicker the SOTN port had to fix). The copy
  makes what the panel sends immutable; it costs one 150 KB memcpy per shown
  frame on the game core.
- **Why two snapshots + one-deep queue**: when the queue is empty the scan task
  holds at most the buffer sent last, so the other one is free to copy into
  (same rule as `copyTask` in the SOTN port). A frame the panel cannot take
  is dropped *before* the copy.
- **Why 8-line bands**: measured faster than 16 on this panel in the ports
  (smaller bursts overlap better with the byte-swap of the next band).
- **Band skipping**: the wire is the bottleneck, so the only way to show more
  frames is to send fewer bytes. Each band's 32-bit rotate-xor hash is computed
  during the byte-swap (data already in registers); identical bands are not
  sent. On straight road the sky does not move, so roughly the top half of the
  screen is skipped. Every 30th frame is sent whole.
- **Memory-write continue**: a run of changed bands opens the window once
  (`CASET`/`RASET` + `RAMWR`) and queues the following bands as pure data
  (`esp_lcd_panel_io_tx_color(io, -1, ...)`), so they do not pay a blocking
  command phase each. Only a skipped band breaks the run.
- **Never wait forever**: every semaphore wait has a 300 ms timeout; a lost
  completion abandons the frame and gives the buffer back (the SOTN port froze
  the panel once because of a `portMAX_DELAY` here). Refused submissions give
  their buffer back too.

## 4.2 Differences from the ports

| Ports | This game |
| --- | --- |
| PSX VRAM in RGB5551, 1024-px stride, 256-px frame pillarboxed | native RGB565, full 320 px, contiguous |
| separate copy task on core 1 | the game task does the memcpy (simpler; the game must not draw into the buffer while it is copied anyway) |
| ESP-IDF project + `esp_lcd_ili9341` component | Arduino sketch: ILI9341 init sequence sent with `esp_lcd_panel_io_tx_param`, no extra library |

## 4.3 Board settings (`board.h`)

| | XIAO handheld (default) | Original dev board |
| --- | --- | --- |
| SPI | SCK 7, MOSI 9, MISO 8, CS 4, DC 43, RST 5 | SCK 12, MOSI 11, MISO 13, CS 10, DC 9, RST 8 |
| Backlight | strapped to 3V3 | GPIO 39 |
| Clock | 40 MHz (80 MHz slips bands on jumper wires) | 40 MHz |
| `MADCTL` | `0xE8` (MY\|MX\|MV\|BGR, rotated 180° for the case) | `0x28` (MV\|BGR, = TFT_eSPI rotation 1) |
| SD CS | GPIO 21 held high | — |

GPIO43 (panel D/C) is UART0 TX on the XIAO. `board.h` refuses to compile
unless **Tools → USB CDC On Boot → Enabled**, otherwise `Serial.begin()` would
take the pin away from the panel (white screen).

## 4.4 Expected numbers

- Full frame at 40 MHz: 153,600 B × 8 / 40 MHz ≈ **30.7 ms** → ~32 panel fps
  when every band changes; more when bands are skipped.
- The game loop is not limited by the panel: it renders the next frame while
  the previous one is on the wire. The HUD shows `FPS` (game), `MS` (CPU time
  per frame) and `LCD` (frames actually shown).
- `displayStats` (offered, dropped, shown, bandsSkipped, timeouts, sendFails,
  lastSendUs, copyUs) can be printed to diagnose the link.

## 4.5 If the screen misbehaves on first boot

| Symptom | Likely cause |
| --- | --- |
| White screen, backlight on | USB CDC On Boot disabled (GPIO43 taken by UART0), or wrong DC pin |
| Image mirrored/upside down | `LCD_MADCTL` (try `0x28`, `0x68`, `0xA8`, `0xE8`) |
| Red and blue swapped | BGR bit (0x08) in `LCD_MADCTL` |
| Bands of the image shifted sideways | SPI too fast for the wiring: keep 40 MHz |
| `lcd: out of memory ... enable Tools > PSRAM > OPI` | PSRAM not enabled |
