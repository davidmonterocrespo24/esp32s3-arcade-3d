#ifndef _TFT_ESPIH_
#define _TFT_ESPIH_

#include <stdint.h>
#include <string>
#include "Arduino.h"

// Color definitions (RGB565)
#define TFT_BLACK       0x0000
#define TFT_NAVY        0x000F
#define TFT_DARKGREEN   0x03E0
#define TFT_DARKCYAN    0x03EF
#define TFT_MAROON      0x7800
#define TFT_PURPLE      0x780F
#define TFT_OLIVE       0x7BE0
#define TFT_LIGHTGREY   0xC618
#define TFT_DARKGREY    0x7BEF
#define TFT_BLUE        0x001F
#define TFT_GREEN       0x07E0
#define TFT_CYAN        0x07FF
#define TFT_RED         0xF800
#define TFT_MAGENTA     0xF81F
#define TFT_YELLOW      0xFFE0
#define TFT_WHITE       0xFFFF
#define TFT_ORANGE      0xFD20
#define TFT_GREENYELLOW 0xAFE5
#define TFT_PINK        0xF81F

// PSRAM fake constant
#define PSRAM_ENABLE 1

class TFT_eSPI {
public:
    TFT_eSPI(int w = 320, int h = 240);
    void begin();
    void setRotation(uint8_t r);
    void fillScreen(uint16_t color);
    uint16_t color565(uint8_t r, uint8_t g, uint8_t b); // Helper if needed
};

class TFT_eSprite {
public:
    TFT_eSprite(TFT_eSPI *tft);
    ~TFT_eSprite();

    void* createSprite(int16_t w, int16_t h);
    void deleteSprite();
    
    void pushSprite(int32_t x, int32_t y);
    void pushToSprite(TFT_eSprite *dspr, int32_t x, int32_t y);
    
    void fillSprite(uint16_t color);
    void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);
    void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);
    void drawFastHLine(int32_t x, int32_t y, int32_t w, uint16_t color);
    void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t color);
    void drawPixel(int32_t x, int32_t y, uint16_t color);
    
    void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint16_t color);
    void fillCircle(int32_t x, int32_t y, int32_t r, uint16_t color);
    void drawCircle(int32_t x, int32_t y, int32_t r, uint16_t color);
    void fillEllipse(int32_t x, int32_t y, int32_t rx, int32_t ry, uint16_t color);
    
    void setTextColor(uint16_t color);
    void setTextColor(uint16_t fg, uint16_t bg);
    void setTextSize(uint8_t size);
    void setCursor(int16_t x, int16_t y);
    
    void print(const char* str);
    void print(String str);
    void print(int n);
    void print(float n);

    void setColorDepth(int8_t b);
    void setAttribute(uint8_t id, uint8_t a);

    // Internal usage for Raylib integration
    void* getTexture(); 

private:
   void* texture; // Raylib RenderTexture2D*
   int16_t _w, _h;
   int16_t cursor_x, cursor_y;
   uint16_t text_color, text_bgcolor;
   uint8_t text_size;
};

#endif
