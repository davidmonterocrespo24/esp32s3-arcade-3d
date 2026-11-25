/*
  ═══════════════════════════════════════════════════════════════
  BOARD PIN MAPS
  Select the board with BOARD below. Pins are GPIO numbers.

  BOARD_XIAO_HANDHELD : the handheld console (Seeed XIAO ESP32S3 Sense +
                        ILI9341 320x240). Same unit and wiring as the
                        mgs_reversing / sotn-decomp ports — see
                        esp32/hardware/HARDWARE.md in those repos.
  BOARD_DEVKIT        : the original ESP32-S3 dev board this game was
                        first built on (pins from the old readme).

  Arduino IDE settings for BOTH boards:
    Tools > PSRAM            : "OPI PSRAM"
    Tools > USB CDC On Boot  : "Enabled"  (mandatory on the XIAO, see below)
  No TFT_eSPI library is needed any more: the panel is driven through
  ESP-IDF's esp_lcd SPI + DMA, exactly like the ports.
  ═══════════════════════════════════════════════════════════════
*/

#ifndef BOARD_H
#define BOARD_H

#define BOARD_XIAO_HANDHELD 1
#define BOARD_DEVKIT        2

#ifndef BOARD
#define BOARD BOARD_XIAO_HANDHELD
#endif

#if BOARD == BOARD_XIAO_HANDHELD
  // ---- LCD: ILI9341 on SPI2, shared with the Sense's microSD (unused here)
  // 40 MHz: measured on this unit with jumper wires. At 80 MHz bands of the
  // image slip sideways. Try 80 only with a short soldered harness.
  #define LCD_SPI_HZ      (40 * 1000 * 1000)
  #define PIN_LCD_SCLK    7     // D8
  #define PIN_LCD_MOSI    9     // D10
  #define PIN_LCD_MISO    8     // D9 (SD only; never read by the panel)
  #define PIN_LCD_CS      4     // D3
  #define PIN_LCD_DC      43    // D6 — also UART0 TX: never start UART0!
  #define PIN_LCD_RST     5     // D4 — real reset line (strapping it high failed)
  #define PIN_LCD_BL      (-1)  // backlight strapped to 3V3
  #define PIN_SD_CS       21    // held high so the card stays off the bus
  // MADCTL 0xE8 = MY|MX|MV|BGR: landscape, rotated 180 to read right in the case
  #define LCD_MADCTL      0xE8

  // ---- Controls
  #define PIN_BTN_A       6     // D5  direct button (active low) -> BRAKE
  #define PIN_BTN_B       44    // D7  direct button (active low) -> TURBO/start
  #define PIN_BTN_LADDER  3     // D2  six buttons on a resistor ladder (ADC)
  #define PIN_STICK_X     1     // D0  ADC
  #define PIN_STICK_Y     2     // D1  ADC
  #define STICK_DEADZONE  60
  #define STICK_INVERT_X  1     // X reads backwards on this unit
  // Ladder windows measured on the unit (10k pull-up), position 5 = START
  #define LADDER_W4_MAX   2450
  #define LADDER_W5_MAX   3100
  #define LADDER_W6_MAX   3700

  #if defined(ARDUINO_ARCH_ESP32) && !ARDUINO_USB_CDC_ON_BOOT
    // GPIO43 is the panel's D/C line. With CDC-on-boot disabled, `Serial` IS
    // UART0 and Serial.begin() would take GPIO43/44 away from the panel and
    // the B button: the screen would stay white.
    #error "XIAO handheld: set Tools > USB CDC On Boot > Enabled"
  #endif

#elif BOARD == BOARD_DEVKIT
  #define LCD_SPI_HZ      (40 * 1000 * 1000)
  #define PIN_LCD_SCLK    12
  #define PIN_LCD_MOSI    11
  #define PIN_LCD_MISO    13
  #define PIN_LCD_CS      10
  #define PIN_LCD_DC      9
  #define PIN_LCD_RST     8
  #define PIN_LCD_BL      39
  #define PIN_SD_CS       (-1)
  // MADCTL 0x28 = MV|BGR: same orientation TFT_eSPI setRotation(1) gave
  #define LCD_MADCTL      0x28

  #define PIN_BTN_LEFT    17
  #define PIN_BTN_RIGHT   16

#else
  #error "Unknown BOARD"
#endif

#endif // BOARD_H
