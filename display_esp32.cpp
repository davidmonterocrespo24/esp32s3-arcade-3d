/*
  ═══════════════════════════════════════════════════════════════
  ESP32-S3 SCANOUT: framebuffer -> ILI9341 over SPI DMA
  Same pipeline the SOTN / MGS ports use on this console:

    game (core 1)                 scanout task (core 0)
    ─────────────                 ─────────────────────
    render into fb (int. SRAM)
    display_present():
      queue busy? -> drop frame
      memcpy fb -> snap[i] (PSRAM) ─queue─> for each 8-line band:
      keep rendering                          byte-swap snap -> DMA chunk
                                              (ping-pong, overlapped with
                                               the previous band's DMA)
                                              skip bands equal to last frame

  Why a snapshot: at 40 MHz a 320x240 frame takes ~31 ms on the wire. If
  the panel read the live buffer the game would lap it and send half-drawn
  frames (flicker). The copy makes what the panel sends immutable.
  Why band skipping: the wire is the bottleneck, so the only way to show
  more frames is to send fewer bytes. Every 30th frame goes out whole.
  ═══════════════════════════════════════════════════════════════
*/

#if defined(ARDUINO_ARCH_ESP32)

#pragma GCC optimize("O2")

#include <Arduino.h>
#include <string.h>
#include "display.h"
#include "board.h"
#include "config.h"
#include "platform.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#define CHUNK_LINES   8                       // measured faster than 16 on this panel
#define CHUNK_PIXELS  (SCR_W * CHUNK_LINES)
#define CHUNK_BYTES   (CHUNK_PIXELS * 2)
#define NUM_BANDS     (SCR_H / CHUNK_LINES)
#define LCD_HOST      SPI2_HOST

typedef uint32_t __attribute__((__may_alias__)) u32a;

DisplayStats displayStats;

static esp_lcd_panel_io_handle_t sIO = nullptr;
static SemaphoreHandle_t sTransDone = nullptr;  // counts free DMA chunk buffers
static QueueHandle_t     sScanQ = nullptr;      // snapshot index for the scan task
static uint16_t*         sChunk[2];             // internal, DMA-capable
static uint16_t*         sSnap[2];              // PSRAM frame snapshots
static int               sLastSent = 1;
static uint32_t          sBandHash[NUM_BANDS];
static bool              sReady = false;

static bool IRAM_ATTR onColorDone(esp_lcd_panel_io_handle_t io,
                                  esp_lcd_panel_io_event_data_t* ev, void* ctx) {
  (void)io; (void)ev; (void)ctx;
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(sTransDone, &woken);
  return woken == pdTRUE;
}

static void lcdCmd(uint8_t cmd, const uint8_t* data, int len) {
  esp_lcd_panel_io_tx_param(sIO, cmd, data, len);
}

// ILI9341 power-on sequence (vendor register values, as used by every
// common ILI9341 driver).
struct InitCmd { uint8_t cmd, len; uint8_t data[15]; };
static const InitCmd ILI9341_INIT[] = {
  { 0xEF, 3,  { 0x03, 0x80, 0x02 } },
  { 0xCF, 3,  { 0x00, 0xC1, 0x30 } },             // power control B
  { 0xED, 4,  { 0x64, 0x03, 0x12, 0x81 } },       // power-on sequence
  { 0xE8, 3,  { 0x85, 0x00, 0x78 } },             // driver timing A
  { 0xCB, 5,  { 0x39, 0x2C, 0x00, 0x34, 0x02 } }, // power control A
  { 0xF7, 1,  { 0x20 } },                         // pump ratio
  { 0xEA, 2,  { 0x00, 0x00 } },                   // driver timing B
  { 0xC0, 1,  { 0x23 } },                         // power control 1
  { 0xC1, 1,  { 0x10 } },                         // power control 2
  { 0xC5, 2,  { 0x3E, 0x28 } },                   // VCOM 1
  { 0xC7, 1,  { 0x86 } },                         // VCOM 2
  { 0x36, 1,  { LCD_MADCTL } },                   // orientation (board.h)
  { 0x3A, 1,  { 0x55 } },                         // 16 bits per pixel
  { 0xB1, 2,  { 0x00, 0x18 } },                   // frame rate 79 Hz
  { 0xB6, 3,  { 0x08, 0x82, 0x27 } },             // display function
  { 0xF2, 1,  { 0x00 } },                         // 3-gamma off
  { 0x26, 1,  { 0x01 } },                         // gamma curve 1
  { 0xE0, 15, { 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1,
                0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00 } },
  { 0xE1, 15, { 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1,
                0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F } },
};

static void setWindow(int x0, int y0, int x1, int y1) {   // inclusive
  uint8_t ca[4] = { (uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1 };
  uint8_t ra[4] = { (uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1 };
  lcdCmd(0x2A, ca, 4);   // tx_param drains in-flight colour DMA first
  lcdCmd(0x2B, ra, 4);
}

// Native RGB565 -> panel byte order, two pixels per 32-bit word, plus a
// cheap fingerprint of the band (a rotate and a xor per word, data already
// in a register).
static inline uint32_t IRAM_ATTR convertBand(uint16_t* dst, const uint16_t* src) {
  const u32a* s = (const u32a*)src;
  u32a* d = (u32a*)dst;
  uint32_t h = 0x811C9DC5u;
  for (int i = 0; i < CHUNK_PIXELS / 2; i++) {
    uint32_t w = s[i];
    h = ((h << 5) | (h >> 27)) ^ w;
    d[i] = ((w & 0x00FF00FFu) << 8) | ((w >> 8) & 0x00FF00FFu);
  }
  return h;
}

static void sendFrame(const uint16_t* src) {
  static uint32_t frames = 0;
  const bool forceFull = (frames++ % 30) == 0;
  int64_t t0 = esp_timer_get_time();
  int buf = 0;
  bool inRun = false;   // true while consecutive bands stream with RAMWR-continue

  for (int band = 0; band < NUM_BANDS; band++) {
    // Never wait forever: a lost completion would freeze the panel while the
    // game keeps running (cost a long diagnosis in the SOTN port).
    if (xSemaphoreTake(sTransDone, pdMS_TO_TICKS(300)) != pdTRUE) {
      displayStats.timeouts++;
      xSemaphoreGive(sTransDone);
      return;   // abandon this frame; the next one repaints
    }
    uint32_t h = convertBand(sChunk[buf], src + band * CHUNK_PIXELS);
    if (!forceFull && h == sBandHash[band]) {
      xSemaphoreGive(sTransDone);   // took a buffer, sent nothing
      displayStats.bandsSkipped++;
      inRun = false;
      continue;
    }
    sBandHash[band] = h;
    esp_err_t err;
    if (!inRun) {
      // New run: window from this band to the bottom, then RAMWR. The
      // following bands are queued as pure data (cmd -1 = memory write
      // continues), so they overlap without any blocking command phase.
      setWindow(0, band * CHUNK_LINES, SCR_W - 1, SCR_H - 1);
      err = esp_lcd_panel_io_tx_color(sIO, 0x2C, sChunk[buf], CHUNK_BYTES);
      inRun = true;
    } else {
      err = esp_lcd_panel_io_tx_color(sIO, -1, sChunk[buf], CHUNK_BYTES);
    }
    if (err != ESP_OK) {
      // No transfer -> no completion callback -> give the buffer back by hand
      displayStats.sendFails++;
      xSemaphoreGive(sTransDone);
      inRun = false;
    }
    buf ^= 1;
  }
  // Drain: both chunk buffers free <=> nothing of this frame left on the bus
  int got = 0;
  for (int i = 0; i < 2; i++)
    if (xSemaphoreTake(sTransDone, pdMS_TO_TICKS(300)) == pdTRUE) got++;
  for (int i = 0; i < got; i++) xSemaphoreGive(sTransDone);
  displayStats.lastSendUs = (uint32_t)(esp_timer_get_time() - t0);
  displayStats.shown++;
}

static void scanTask(void* arg) {
  (void)arg;
  for (;;) {
    int idx;
    if (xQueueReceive(sScanQ, &idx, portMAX_DELAY) == pdTRUE) sendFrame(sSnap[idx]);
  }
}

bool display_init() {
  memset(&displayStats, 0, sizeof(displayStats));

  if (PIN_SD_CS >= 0) {               // keep the microSD off the shared bus
    pinMode(PIN_SD_CS, OUTPUT);
    digitalWrite(PIN_SD_CS, HIGH);
  }
  if (PIN_LCD_BL >= 0) {
    pinMode(PIN_LCD_BL, OUTPUT);
    digitalWrite(PIN_LCD_BL, LOW);    // backlight on only after the first clear
  }

  spi_bus_config_t bus;
  memset(&bus, 0, sizeof(bus));
  bus.mosi_io_num = PIN_LCD_MOSI;
  bus.miso_io_num = PIN_LCD_MISO;
  bus.sclk_io_num = PIN_LCD_SCLK;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.data4_io_num = -1;
  bus.data5_io_num = -1;
  bus.data6_io_num = -1;
  bus.data7_io_num = -1;
  bus.max_transfer_sz = CHUNK_BYTES + 64;
  if (spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
    plat_log("lcd: spi_bus_initialize failed\n");
    return false;
  }

  sTransDone = xSemaphoreCreateCounting(2, 2);
  sScanQ = xQueueCreate(1, sizeof(int));

  esp_lcd_panel_io_spi_config_t io;
  memset(&io, 0, sizeof(io));
  io.cs_gpio_num = PIN_LCD_CS;
  io.dc_gpio_num = PIN_LCD_DC;
  io.spi_mode = 0;
  io.pclk_hz = LCD_SPI_HZ;
  io.trans_queue_depth = 8;
  io.on_color_trans_done = onColorDone;
  io.user_ctx = nullptr;
  io.lcd_cmd_bits = 8;
  io.lcd_param_bits = 8;
  if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io, &sIO) != ESP_OK) {
    plat_log("lcd: esp_lcd_new_panel_io_spi failed\n");
    return false;
  }

  // Hardware reset: on this panel a strapped RST + software reset was not
  // reliable, so the board wires a real GPIO.
  if (PIN_LCD_RST >= 0) {
    pinMode(PIN_LCD_RST, OUTPUT);
    digitalWrite(PIN_LCD_RST, LOW);
    vTaskDelay(pdMS_TO_TICKS(20));
    digitalWrite(PIN_LCD_RST, HIGH);
    vTaskDelay(pdMS_TO_TICKS(150));
  } else {
    lcdCmd(0x01, nullptr, 0);
    vTaskDelay(pdMS_TO_TICKS(150));
  }
  for (size_t i = 0; i < sizeof(ILI9341_INIT) / sizeof(ILI9341_INIT[0]); i++)
    lcdCmd(ILI9341_INIT[i].cmd, ILI9341_INIT[i].data, ILI9341_INIT[i].len);
  lcdCmd(0x11, nullptr, 0);           // sleep out
  vTaskDelay(pdMS_TO_TICKS(120));
  lcdCmd(0x29, nullptr, 0);           // display on

  for (int i = 0; i < 2; i++) {
    sChunk[i] = (uint16_t*)heap_caps_malloc(CHUNK_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    sSnap[i]  = (uint16_t*)heap_caps_malloc(SCR_W * SCR_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!sChunk[i] || !sSnap[i]) {
      plat_log("lcd: out of memory (chunk %p snap %p) - enable Tools > PSRAM > OPI\n",
               sChunk[i], sSnap[i]);
      return false;
    }
  }

  // Black frame, then backlight on
  memset(sChunk[0], 0, CHUNK_BYTES);
  setWindow(0, 0, SCR_W - 1, SCR_H - 1);
  for (int band = 0; band < NUM_BANDS; band++) {
    xSemaphoreTake(sTransDone, portMAX_DELAY);
    esp_lcd_panel_io_tx_color(sIO, band == 0 ? 0x2C : -1, sChunk[0], CHUNK_BYTES);
  }
  for (int i = 0; i < 2; i++) xSemaphoreTake(sTransDone, portMAX_DELAY);
  for (int i = 0; i < 2; i++) xSemaphoreGive(sTransDone);
  if (PIN_LCD_BL >= 0) digitalWrite(PIN_LCD_BL, HIGH);

  // Scanout on core 0; the Arduino loop (the game) runs on core 1.
  if (xTaskCreatePinnedToCore(scanTask, "lcd_scan", 4096, nullptr, 5, nullptr, 0) != pdPASS) {
    plat_log("lcd: scan task FAILED to start\n");
    return false;
  }
  sReady = true;
  plat_log("lcd: ILI9341 %d MHz, %d-line DMA bands, snapshots in PSRAM\n",
           LCD_SPI_HZ / 1000000, CHUNK_LINES);
  return true;
}

void display_present(const uint16_t* fb) {
  if (!sReady) return;
  displayStats.offered++;
  // One-deep queue: if a frame is still waiting, the scan task is busy with
  // the one before it. Drop this frame BEFORE paying for the copy.
  if (uxQueueMessagesWaiting(sScanQ) > 0) {
    displayStats.dropped++;
    return;
  }
  // Queue empty => the scan task holds at most sLastSent, so its sibling is free.
  int cur = sLastSent ^ 1;
  int64_t t0 = esp_timer_get_time();
  memcpy(sSnap[cur], fb, SCR_W * SCR_H * 2);
  displayStats.copyUs = (uint32_t)(esp_timer_get_time() - t0);
  if (xQueueSend(sScanQ, &cur, 0) == pdTRUE) sLastSent = cur;
  else displayStats.dropped++;
}

#endif // ARDUINO_ARCH_ESP32
