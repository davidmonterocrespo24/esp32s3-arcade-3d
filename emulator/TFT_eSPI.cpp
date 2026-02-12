#include "TFT_eSPI.h"
#include "raylib.h"
#include <cmath>

// Helper to convert RGB565 to Raylib Color
Color r565(uint16_t c) {
    int r = (c >> 11) * 255 / 31;
    int g = ((c >> 5) & 0x3F) * 255 / 63;
    int b = (c & 0x1F) * 255 / 31;
    return (Color){(unsigned char)r, (unsigned char)g, (unsigned char)b, 255};
}

// ---------------- TFT_eSPI ----------------
TFT_eSPI::TFT_eSPI(int w, int h) {
    // Nothing special needed for now
}

void TFT_eSPI::begin() {
    // Initialized in main usually
}

void TFT_eSPI::setRotation(uint8_t r) {
    // Loop
}

void TFT_eSPI::fillScreen(uint16_t color) {
    ClearBackground(r565(color));
}

// ---------------- TFT_eSprite ----------------

 struct SpriteData {
    RenderTexture2D texture;
    bool created;
};

TFT_eSprite::TFT_eSprite(TFT_eSPI *tft) {
    texture = new SpriteData{0};
    _w = 0;
    _h = 0;
    cursor_x = 0;
    cursor_y = 0;
    text_color = 0xFFFF;
    text_size = 1;
}

TFT_eSprite::~TFT_eSprite() {
    SpriteData* sd = (SpriteData*)texture;
    if (sd->created) UnloadRenderTexture(sd->texture);
    delete sd;
}

void* TFT_eSprite::createSprite(int16_t w, int16_t h) {
    SpriteData* sd = (SpriteData*)texture;
    if (sd->created) UnloadRenderTexture(sd->texture);
    
    sd->texture = LoadRenderTexture(w, h);
    sd->created = true;
    _w = w;
    _h = h;
    return (void*)sd; // Return non-null to indicate success
}

void TFT_eSprite::deleteSprite() {
    SpriteData* sd = (SpriteData*)texture;
    if (sd->created) {
        UnloadRenderTexture(sd->texture);
        sd->created = false;
    }
}

void TFT_eSprite::pushSprite(int32_t x, int32_t y) {
    SpriteData* sd = (SpriteData*)texture;
    if (!sd->created) return;
    
    // Raylib's textures are upside down compared to what we expect usually if not handled
    // But LoadRenderTexture creates standard GL texture.
    // We draw it to the screen.
    DrawTextureRec(sd->texture.texture, (Rectangle){0, 0, (float)sd->texture.texture.width, (float)-sd->texture.texture.height}, (Vector2){(float)x, (float)y}, WHITE);
}

void TFT_eSprite::pushToSprite(TFT_eSprite *dspr, int32_t x, int32_t y) {
   SpriteData* sd = (SpriteData*)texture;
   SpriteData* target = (SpriteData*)dspr->texture;
   
   if (!sd->created || !target->created) return;
   
   BeginTextureMode(target->texture);
   DrawTextureRec(sd->texture.texture, (Rectangle){0, 0, (float)sd->texture.texture.width, (float)-sd->texture.texture.height}, (Vector2){(float)x, (float)y}, WHITE);
   EndTextureMode();
}

void TFT_eSprite::setColorDepth(int8_t b) {}
void TFT_eSprite::setAttribute(uint8_t id, uint8_t a) {}

// Drawing primitives

void TFT_eSprite::fillSprite(uint16_t color) {
    SpriteData* sd = (SpriteData*)texture;
    if (!sd->created) return;
    BeginTextureMode(sd->texture);
    ClearBackground(r565(color));
    EndTextureMode();
}

void TFT_eSprite::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) {
    SpriteData* sd = (SpriteData*)texture;
    if (!sd->created) return;
    BeginTextureMode(sd->texture);
    DrawRectangle(x, y, w, h, r565(color));
    EndTextureMode();
}

void TFT_eSprite::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) {
    SpriteData* sd = (SpriteData*)texture;
    if (!sd->created) return;
    BeginTextureMode(sd->texture);
    DrawRectangleLines(x, y, w, h, r565(color));
    EndTextureMode();
}

void TFT_eSprite::drawFastHLine(int32_t x, int32_t y, int32_t w, uint16_t color) {
    SpriteData* sd = (SpriteData*)texture;
    if (!sd->created) return;
    BeginTextureMode(sd->texture);
    DrawLine(x, y, x + w, y, r565(color));
    EndTextureMode();
}

void TFT_eSprite::drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t color) {
    SpriteData* sd = (SpriteData*)texture;
    if (!sd->created) return;
    BeginTextureMode(sd->texture);
    DrawLine(x0, y0, x1, y1, r565(color));
    EndTextureMode();
}

void TFT_eSprite::drawPixel(int32_t x, int32_t y, uint16_t color) {
    SpriteData* sd = (SpriteData*)texture;
    if (!sd->created) return;
    BeginTextureMode(sd->texture);
    DrawPixel(x, y, r565(color));
    EndTextureMode();
}

void TFT_eSprite::fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint16_t color) {
    SpriteData* sd = (SpriteData*)texture;
    if (!sd->created) return;
    BeginTextureMode(sd->texture);
    // Draw twice to handle both winding orders (Raylib culls CW by default in some versions/contexts)
    DrawTriangle((Vector2){(float)x0, (float)y0}, (Vector2){(float)x1, (float)y1}, (Vector2){(float)x2, (float)y2}, r565(color));
    DrawTriangle((Vector2){(float)x0, (float)y0}, (Vector2){(float)x2, (float)y2}, (Vector2){(float)x1, (float)y1}, r565(color));
    EndTextureMode();
}

void TFT_eSprite::fillCircle(int32_t x, int32_t y, int32_t r, uint16_t color) {
    SpriteData* sd = (SpriteData*)texture;
    if (!sd->created) return;
    BeginTextureMode(sd->texture);
    DrawCircle(x, y, r, r565(color));
    EndTextureMode();
}

void TFT_eSprite::drawCircle(int32_t x, int32_t y, int32_t r, uint16_t color) {
    SpriteData* sd = (SpriteData*)texture;
    if (!sd->created) return;
    BeginTextureMode(sd->texture);
    DrawCircleLines(x, y, r, r565(color));
    EndTextureMode();
}

void TFT_eSprite::fillEllipse(int32_t x, int32_t y, int32_t rx, int32_t ry, uint16_t color) {
    SpriteData* sd = (SpriteData*)texture;
    if (!sd->created) return;
    BeginTextureMode(sd->texture);
    DrawEllipse(x, y, rx, ry, r565(color));
    EndTextureMode();
}

void TFT_eSprite::setTextColor(uint16_t color) {
    text_color = color;
}

void TFT_eSprite::setTextColor(uint16_t fg, uint16_t bg) {
    text_color = fg;
    text_bgcolor = bg;
}

void TFT_eSprite::setTextSize(uint8_t size) {
    text_size = size;
}

void TFT_eSprite::setCursor(int16_t x, int16_t y) {
    cursor_x = x;
    cursor_y = y;
}

void TFT_eSprite::print(const char* str) {
    SpriteData* sd = (SpriteData*)texture;
    if (!sd->created) return;
    BeginTextureMode(sd->texture);
    DrawText(str, cursor_x, cursor_y, text_size * 10, r565(text_color)); // Simple scaling
    EndTextureMode();
    // Update cursor? (Simulated)
    cursor_x += MeasureText(str, text_size * 10);
}

void TFT_eSprite::print(String str) {
    print(str.c_str());
}

void TFT_eSprite::print(int n) {
    print(std::to_string(n).c_str());
}

void TFT_eSprite::print(float n) {
    char p[16];
    sprintf(p, "%.2f", n);
    print(p);
}
