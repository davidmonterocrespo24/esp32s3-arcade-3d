/*
  ═══════════════════════════════════════════════════════════════
  ESP32 Pseudo-3D Racing Game — TFT_eSPI (Double Buffered)
  ═══════════════════════════════════════════════════════════════
*/

#include <SPI.h>
#include <TFT_eSPI.h> // Reemplaza Adafruit_GFX y Adafruit_ILI9341

// ═══════════════════════════════════════════════════════════════
//  HARDWARE PINS (Los de la pantalla van en User_Setup.h)
// ═══════════════════════════════════════════════════════════════
#define TFT_BL    39

// Botones
#define BTN_LEFT  17
#define BTN_RIGHT 16

TFT_eSPI tft = TFT_eSPI(); 
TFT_eSprite spr = TFT_eSprite(&tft); // Nuestro Buffer en RAM

// ═══════════════════════════════════════════════════════════════
//  GAME CONSTANTS
// ═══════════════════════════════════════════════════════════════
#define SCR_W       320
#define SCR_H       240
#define SCR_CX      (SCR_W / 2)
#define SCR_CY      (SCR_H / 2)

#define SEG_LEN     200       
#define RUMBLE_LEN  3         
#define DRAW_DIST   40        
#define TOTAL_SEGS  300       
#define ROAD_W      2000      
#define LANES       3         
#define FOV_DEG     100       
#define CAM_HEIGHT  1000      
#define FOG_DENSITY 5         

// ═══════════════════════════════════════════════════════════════
//  DATA STRUCTURES
// ═══════════════════════════════════════════════════════════════
struct Segment {
  float curve;              
  float y;                  
  int8_t spriteType;        
  float  spriteOffset;      
  bool   tunnel;            // true = dentro de túnel
};

struct RenderPt {
  int16_t x, y, w;          
  float   scale;            
};

struct TrafficCar {
  float    offset;          
  float    z;               
  float    speed;           
  uint16_t color;           
};

// ═══════════════════════════════════════════════════════════════
//  GLOBALS
// ═══════════════════════════════════════════════════════════════
Segment  segments[TOTAL_SEGS];
int      segCount = 0;
float    trackLength;          

float cameraDepth;             
float playerZdist;             

float position   = 0;         
float playerX    = 0;         
float speed      = 0;
float maxSpeed;                
float centrifugal = 0.3f;

bool  crashed     = false;
unsigned long crashTimer = 0;

unsigned long lastFrameMs;
float currentLapTime = 0;
float lastLapTime    = 0;
float bestLapTime    = 0;
float prevPosition   = 0;

#define MAX_CARS 6
TrafficCar trafficCars[MAX_CARS];

int  timeOfDay = 0;            
long distSinceTimeChange = 0;

RenderPt rCache[DRAW_DIST];
int16_t  rClip [DRAW_DIST];

// ═══════════════════════════════════════════════════════════════
//  COLOUR PALETTE
// ═══════════════════════════════════════════════════════════════
uint16_t colSky1, colSky2, colSky3;
uint16_t colGrassL, colGrassD;
uint16_t colRoadL, colRoadD;
uint16_t colRumbleL, colRumbleD;
uint16_t colLane, colFog;

// ═══════════════════════════════════════════════════════════════
//  UTILITY FUNCTIONS
// ═══════════════════════════════════════════════════════════════
static inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
static inline float clampF(float v, float lo, float hi) {
  return (v < lo) ? lo : (v > hi) ? hi : v;
}
static inline float lerpF(float a, float b, float t) {
  return a + (b - a) * t;
}
static float easeIn(float a, float b, float t) {
  return a + (b - a) * t * t;
}
static float easeInOut(float a, float b, float t) {
  return a + (b - a) * (-cosf(t * PI) * 0.5f + 0.5f);
}
static float loopIncrease(float v, float inc, float mx) {
  float r = v + inc;
  while (r >= mx) r -= mx;
  while (r < 0)   r += mx;
  return r;
}
static float percentRemaining(float v, float total) {
  float r = fmodf(v, total);
  if (r < 0) r += total;
  return r / total;
}
static int findSegIdx(float z) {
  int i = (int)floorf(z / SEG_LEN) % TOTAL_SEGS;
  if (i < 0) i += TOTAL_SEGS;
  return i;
}

uint16_t darkenCol(uint16_t c, float f) {
  int r = (int)(((c >> 11) & 0x1F) * f);
  int g = (int)(((c >> 5)  & 0x3F) * f);
  int b = (int)((c & 0x1F) * f);
  return ((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F);
}

uint16_t lerpCol(uint16_t c1, uint16_t c2, float t) {
  if (t <= 0) return c1;
  if (t >= 1) return c2;
  int r1 = (c1 >> 11) & 0x1F, g1 = (c1 >> 5) & 0x3F, b1 = c1 & 0x1F;
  int r2 = (c2 >> 11) & 0x1F, g2 = (c2 >> 5) & 0x3F, b2 = c2 & 0x1F;
  return (((int)(r1 + (r2 - r1) * t) & 0x1F) << 11) |
         (((int)(g1 + (g2 - g1) * t) & 0x3F) << 5)  |
          ((int)(b1 + (b2 - b1) * t) & 0x1F);
}

float expFog(float d, float density) {
  return 1.0f - clampF(1.0f / expf(d * d * density), 0, 1);
}

bool overlapChk(float x1, float w1, float x2, float w2) {
  float h1 = w1 * 0.5f, h2 = w2 * 0.5f;
  return !((x1 + h1) < (x2 - h2) || (x1 - h1) > (x2 + h2));
}

// ═══════════════════════════════════════════════════════════════
//  COLOUR INIT
// ═══════════════════════════════════════════════════════════════
void initColors() {
  switch (timeOfDay) {
    case 0: // Day
      colSky1    = rgb(115, 185, 255);
      colSky2    = rgb(80, 140, 230);
      colSky3    = rgb(50, 100, 200);
      colGrassL  = rgb(16, 200, 16);
      colGrassD  = rgb(0, 154, 0);
      colRoadL   = rgb(107, 107, 107);
      colRoadD   = rgb(105, 105, 105);
      colRumbleL = TFT_WHITE;
      colRumbleD = TFT_RED;
      colLane    = rgb(204, 204, 204);
      colFog     = rgb(180, 215, 255);
      break;
    case 1: // Sunset
      colSky1    = rgb(255, 140, 60);
      colSky2    = rgb(255, 90, 30);
      colSky3    = rgb(140, 30, 10);
      colGrassL  = rgb(80, 120, 20);
      colGrassD  = rgb(50, 80, 10);
      colRoadL   = rgb(95, 85, 80);
      colRoadD   = rgb(85, 75, 70);
      colRumbleL = rgb(240, 200, 150);
      colRumbleD = rgb(200, 50, 30);
      colLane    = rgb(200, 180, 140);
      colFog     = rgb(200, 120, 60);
      break;
    default: // Night
      colSky1    = rgb(10, 15, 50);
      colSky2    = rgb(5, 8, 30);
      colSky3    = rgb(0, 2, 15);
      colGrassL  = rgb(0, 60, 15);
      colGrassD  = rgb(0, 40, 8);
      colRoadL   = rgb(55, 55, 65);
      colRoadD   = rgb(45, 45, 55);
      colRumbleL = rgb(100, 100, 120);
      colRumbleD = rgb(80, 10, 10);
      colLane    = rgb(110, 110, 130);
      colFog     = rgb(15, 15, 40);
      break;
  }
}

// ═══════════════════════════════════════════════════════════════
//  TRACK BUILDER
// ═══════════════════════════════════════════════════════════════
float lastY() {
  return (segCount == 0) ? 0 : segments[(segCount - 1) % TOTAL_SEGS].y;
}

void addSeg(float curve, float y, bool isTunnel = false) {
  if (segCount >= TOTAL_SEGS) return;
  segments[segCount].curve        = curve;
  segments[segCount].y            = y;
  segments[segCount].spriteType   = -1;
  segments[segCount].spriteOffset = 0;
  segments[segCount].tunnel       = isTunnel;
  segCount++;
}

void addRoad(int enter, int hold, int leave, float curve, float hillY) {
  float sY  = lastY();
  float eY  = sY + hillY * SEG_LEN;
  int total = enter + hold + leave;
  for (int n = 0; n < enter; n++)
    addSeg(easeIn(0, curve, (float)n / enter),
           easeInOut(sY, eY, (float)n / total));
  for (int n = 0; n < hold; n++)
    addSeg(curve, easeInOut(sY, eY, (float)(enter + n) / total));
  for (int n = 0; n < leave; n++)
    addSeg(easeInOut(curve, 0, (float)n / leave),
           easeInOut(sY, eY, (float)(enter + hold + n) / total));
}

void addSprite(int idx, int type, float off) {
  if (idx >= 0 && idx < segCount) {
    segments[idx].spriteType   = type;
    segments[idx].spriteOffset = off;
  }
}

// Crear un túnel recto (con curva opcional)
void addTunnel(int length, float curve = 0) {
  int startSeg = segCount;
  float sY = lastY();
  for (int n = 0; n < length; n++) {
    if (segCount >= TOTAL_SEGS) break;
    float c = 0;
    if (curve != 0) {
      float t = (float)n / length;
      if (t < 0.25)      c = easeIn(0, curve, t * 4);
      else if (t < 0.75) c = curve;
      else               c = easeInOut(curve, 0, (t - 0.75) * 4);
    }
    addSeg(c, sY, true);
  }
  // Arco de entrada
  addSprite(startSeg, 6, 0.0);
  // Arco de salida
  if (segCount > startSeg + 2)
    addSprite(segCount - 1, 6, 0.0);
}

// Decorar los bordes con edificios de ciudad
void addCityDecorations() {
  for (int n = 5; n < segCount; n++) {
    if (segments[n].tunnel) continue; // No dentro del túnel
    if (segments[n].spriteType >= 0) continue; // Ya tiene sprite
    int r = random(0, 100);
    if (r < 18) {
      // Edificio a la izquierda
      addSprite(n, 5, -1.5 - random(0, 15) / 10.0);
    } else if (r < 36) {
      // Edificio a la derecha
      addSprite(n, 5,  1.5 + random(0, 15) / 10.0);
    } else if (r < 40) {
      // Poste (farola)
      addSprite(n, 4, (random(0, 2) == 0) ? -1.1 : 1.1);
    } else if (r < 44) {
      // Árbol decorativo
      addSprite(n, random(0, 2), (random(0, 2) == 0) ? -1.3 : 1.3);
    }
  }
}

void buildTrack() {
  segCount = 0;

  // === ZONA 1: Recta de salida con ciudad ===
  addRoad(25, 25, 25, 0, 0);

  // === ZONA 2: Primer túnel recto ===
  addTunnel(35);

  // === ZONA 3: Colinas suaves ===
  addRoad(25, 25, 25, 0,  10);
  addRoad(25, 25, 25, 0, -20);

  // === ZONA 4: Curvas S con edificios ===
  addRoad(50, 50, 50, -2.0, 0);
  addRoad(50, 50, 50,  4.0, 20);

  // === ZONA 5: Túnel con curva ===
  addTunnel(40, 3.0);

  // === ZONA 6: Más curvas y colinas ===
  addRoad(50, 50, 50,  2.0, -20);
  addRoad(50, 50, 50, -2.0, 30);
  addRoad(50, 50, 50, -4.0, -30);

  // === ZONA 7: Recta larga ===
  addRoad(25, 25, 25, 0, 0);

  // === ZONA 8: Curva cerrada + colina grande ===
  addRoad(80, 80, 80, 6.0, 0);
  addRoad(40, 40, 40, 0, 40);

  // === ZONA 9: Túnel final antes de meta ===
  addTunnel(30, -2.0);

  // Rellenar y volver a Y=0
  int rem = TOTAL_SEGS - segCount;
  if (rem > 3) {
    float curY = lastY();
    addRoad(rem / 3, rem / 3, rem - 2 * (rem / 3),
            -2.0, -curY / SEG_LEN);
  }
  while (segCount < TOTAL_SEGS) addSeg(0, 0);
  trackLength = (float)TOTAL_SEGS * SEG_LEN;

  // Colocar edificios y decoración de ciudad
  addCityDecorations();
}

// ═══════════════════════════════════════════════════════════════
//  TRAFFIC INIT
// ═══════════════════════════════════════════════════════════════
void initTraffic() {
  const uint16_t cols[] = {
    rgb(0,80,220),  rgb(220,200,0), rgb(200,200,200),
    rgb(0,180,80),  rgb(255,100,0), rgb(160,0,200),
    rgb(0,180,180), rgb(180,60,60), rgb(255,180,200),
    rgb(100,100,100), rgb(0,120,0), rgb(200,150,0)
  };
  for (int i = 0; i < MAX_CARS; i++) {
    trafficCars[i].offset = random(-8, 9) / 10.0;
    trafficCars[i].z      = random(0, TOTAL_SEGS) * SEG_LEN;
    trafficCars[i].speed  = maxSpeed * (0.2 + random(0, 50) / 100.0);
    trafficCars[i].color  = cols[i % 6];
  }
}

// ═══════════════════════════════════════════════════════════════
//  DRAWING HELPERS (Ahora usamos spr. en lugar de tft.)
// ═══════════════════════════════════════════════════════════════
void drawSpriteShape(int type, int sx, int sy, float scale, int16_t clipY) {
  int bottomY = min((int)sy, (int)clipY);
  if (bottomY <= 0 || sx < -60 || sx > SCR_W + 60) return;

  switch (type) {
    case 0: { 
      int h = (int)(scale * 28000);
      int w = (int)(scale * 10000);
      if (h < 3 || w < 2) return;
      int trunkH = h / 4, trunkW = max(2, w / 5);
      uint16_t gc = (timeOfDay == 2) ? rgb(0, 50, 10) : rgb(0, 130, 25);
      spr.fillRect(sx - trunkW / 2, bottomY - trunkH, trunkW, trunkH, rgb(80, 50, 20));
      spr.fillTriangle(sx, bottomY - h, sx - w / 2, bottomY - trunkH, sx + w / 2, bottomY - trunkH, gc);
      break;
    }
    case 1: { 
      int h = (int)(scale * 26000);
      int w = (int)(scale * 12000);
      if (h < 3 || w < 2) return;
      int trunkH = h * 2 / 3, trunkW = max(2, w / 8);
      int leafR  = max(2, w / 2);
      uint16_t lc = (timeOfDay == 2) ? rgb(0, 55, 12) : rgb(0, 140, 30);
      spr.fillRect(sx - trunkW / 2, bottomY - trunkH, trunkW, trunkH, rgb(120, 80, 30));
      spr.fillCircle(sx, bottomY - trunkH - leafR / 2, leafR, lc);
      break;
    }
    case 2: { 
      int r = max(2, (int)(scale * 6000));
      uint16_t bc = (timeOfDay == 2) ? rgb(0, 48, 10) : rgb(20, 130, 20);
      spr.fillCircle(sx, bottomY - r, r, bc);
      break;
    }
    case 3: { 
      int rh = max(2, (int)(scale * 6000));
      int rw = max(3, (int)(scale * 8000));
      spr.fillRect(sx - rw / 2, bottomY - rh, rw, rh, rgb(100, 95, 90));
      break;
    }
    case 4: { 
      int ph = max(3, (int)(scale * 14000));
      int pw = max(2, 3);
      spr.fillRect(sx - pw / 2, bottomY - ph, pw, ph, TFT_WHITE);
      if (ph > 6)
        spr.fillRect(sx - pw / 2, bottomY - ph, pw, 3, TFT_RED);
      break;
    }
    case 5: { // Edificio de ciudad
      int bh = max(10, (int)(scale * 100000));
      int bw = max(5, (int)(scale * 40000));
      if (bh < 4 || bw < 3) return;
      // Color de fachada según hora del día
      uint16_t bCol, winCol;
      if (timeOfDay == 2) {
        bCol   = rgb(20, 20, 35);
        winCol = rgb(200, 200, 80); // Ventanas iluminadas de noche
      } else if (timeOfDay == 1) {
        bCol   = rgb(120, 100, 90);
        winCol = rgb(220, 180, 120);
      } else {
        bCol   = rgb(140, 140, 155);
        winCol = rgb(170, 210, 230);
      }
      // Cuerpo del edificio
      spr.fillRect(sx - bw / 2, bottomY - bh, bw, bh, bCol);
      // Borde/cornisa
      spr.drawRect(sx - bw / 2, bottomY - bh, bw, bh, darkenCol(bCol, 0.6));
      // Ventanas (grid)
      if (bh > 20 && bw > 12) {
        int wRows = min(6, bh / 8);
        int wCols = min(3, bw / 8);
        int ww = max(2, bw / (wCols * 2 + 1));
        int wh = max(2, bh / (wRows * 2 + 1));
        for (int wr = 0; wr < wRows; wr++) {
          for (int wc = 0; wc < wCols; wc++) {
            int wx = sx - bw / 2 + (wc + 1) * bw / (wCols + 1) - ww / 2;
            int wy = bottomY - bh + (wr + 1) * bh / (wRows + 1) - wh / 2;
            spr.fillRect(wx, wy, ww, wh, winCol);
          }
        }
      }
      // Techo/antena en edificios altos
      if (bh > 40 && bw > 8) {
        spr.fillRect(sx - 1, bottomY - bh - bh / 6, 3, bh / 6, darkenCol(bCol, 0.7));
      }
      break;
    }
    case 6: { // Arco de entrada/salida de túnel
      int th = max(10, (int)(scale * 80000));
      int tw = max(10, (int)(scale * 140000));
      if (th < 5 || tw < 5) return;
      // Fachada de piedra
      uint16_t stoneCol = rgb(60, 55, 50);
      spr.fillRect(sx - tw / 2, bottomY - th, tw, th, stoneCol);
      // Abertura semicircular (negro)
      int arcW = tw * 3 / 4;
      int arcH = th * 4 / 5;
      spr.fillRect(sx - arcW / 2, bottomY - arcH, arcW, arcH, TFT_BLACK);
      // Arco superior redondeado
      int arcR = arcW / 2;
      spr.fillCircle(sx, bottomY - arcH, arcR, TFT_BLACK);
      // Borde del arco
      spr.drawCircle(sx, bottomY - arcH, arcR, rgb(80, 75, 70));
      // Piedras clave
      spr.fillRect(sx - 2, bottomY - th, 5, max(2, th / 8), rgb(90, 85, 75));
      break;
    }
  }
}

void drawTrafficCar(int cx, int cy, float scale, uint16_t col, int16_t clipY) {
  int w = (int)(scale * 30000);
  int h = (int)(scale * 18000);
  if (w < 3 || h < 2) return;
  int bottomY = min(cy, (int)clipY);
  int topY = bottomY - h;
  if (topY >= SCR_H || bottomY < 0) return;

  spr.fillRect(cx - w / 2, topY, w, h * 2 / 3, col);
  int rw = w * 2 / 3;
  spr.fillRect(cx - rw / 2, topY - h / 4, rw, h / 3, darkenCol(col, 0.8));
  if (h > 6) {
    int ww = rw - 4;
    if (ww > 2)
      spr.fillRect(cx - ww / 2, topY - h / 4 + 1, ww, max(1, h / 5), rgb(150, 200, 255));
  }
  if (w > 8) {
    spr.fillRect(cx - w / 2 + 1, topY + 1, 2, max(1, h / 6), TFT_RED);
    spr.fillRect(cx + w / 2 - 3, topY + 1, 2, max(1, h / 6), TFT_RED);
  }
}

void drawPlayerCar() {
  const int cW = 44, cH = 28;
  int cx = SCR_CX, cy = SCR_H - 20;

  spr.fillRect(cx - cW / 2 + 3, cy + 2, cW - 6, 4, rgb(30, 30, 30));
  spr.fillRect(cx - cW / 2 - 3, cy - 12, 5, 14, rgb(20, 20, 20));
  spr.fillRect(cx + cW / 2 - 2, cy - 12, 5, 14, rgb(20, 20, 20));
  spr.fillRect(cx - cW / 2, cy - 18, cW, 18, rgb(220, 20, 20));
  int rw = cW - 16;
  spr.fillRect(cx - rw / 2, cy - cH, rw, 14, rgb(200, 15, 15));
  spr.fillRect(cx - rw / 2 + 3, cy - cH + 2, rw - 6, 6, rgb(100, 180, 255));
  spr.fillRect(cx - cW / 2 + 2, cy - 14, 6, 4, TFT_ORANGE);
  spr.fillRect(cx + cW / 2 - 8, cy - 14, 6, 4, TFT_ORANGE);
  spr.drawFastHLine(cx - 1, cy - 18, 3, TFT_WHITE);
  spr.drawFastHLine(cx - 1, cy - 10, 3, TFT_WHITE);
}

void drawSky() {
  // Si el jugador está dentro de un túnel, dibujar techo oscuro
  int pSegIdx = findSegIdx(position + playerZdist);
  if (segments[pSegIdx].tunnel) {
    uint16_t roofCol = rgb(25, 22, 20);
    spr.fillRect(0, 0, SCR_W, SCR_CY, roofCol);
    // Luces del túnel (puntos amarillos en el techo)
    for (int i = 0; i < 5; i++) {
      int lx = 40 + i * 60;
      spr.fillCircle(lx, SCR_CY - 10, 3, rgb(200, 180, 50));
      spr.fillCircle(lx, SCR_CY - 10, 1, rgb(255, 255, 150));
    }
    return;
  }

  int h = SCR_CY, band = h / 3;
  spr.fillRect(0, 0,      SCR_W, band,         colSky1);
  spr.fillRect(0, band,   SCR_W, band,         colSky2);
  spr.fillRect(0, band*2, SCR_W, h - band * 2, colSky3);

  if (timeOfDay == 2) {
    static const uint16_t stX[] = {15,45,78,120,155,190,225,260,290,310,33,67,105,145,185,230,275};
    static const uint8_t stY[] = {8,25,15,5,30,12,22,8,18,28,40,48,35,50,42,55,38};
    for (int i = 0; i < 17; i++) spr.drawPixel(stX[i], stY[i], TFT_WHITE);
  }
  spr.drawFastHLine(0, h, SCR_W, lerpCol(colSky3, TFT_WHITE, 0.3));
}

void drawRoad() {
  int baseIdx = findSegIdx(position);
  float basePct = percentRemaining(position, SEG_LEN);
  float posOff  = fmodf(position, (float)SEG_LEN);
  if (posOff < 0) posOff += SEG_LEN;

  int pSegIdx  = findSegIdx(position + playerZdist);
  int pPrevIdx = (pSegIdx - 1 + TOTAL_SEGS) % TOTAL_SEGS;
  float pPct   = percentRemaining(position + playerZdist, SEG_LEN);
  float playerY = lerpF(segments[pPrevIdx].y, segments[pSegIdx].y, pPct);
  float camY    = playerY + CAM_HEIGHT;

  float curveX  = 0;
  float curveDX = -(segments[baseIdx].curve * basePct);
  int maxy = SCR_H; 

  for (int n = 0; n < DRAW_DIST; n++) {
    int sIdx = (baseIdx + n) % TOTAL_SEGS;
    int prev = (sIdx - 1 + TOTAL_SEGS) % TOTAL_SEGS;
    Segment& seg = segments[sIdx];

    float camZ1 = (float)n       * SEG_LEN - posOff;
    float camZ2 = (float)(n + 1) * SEG_LEN - posOff;

    rCache[n] = {(int16_t)SCR_CX, (int16_t)SCR_H, 0, 0.0f};
    rClip[n]  = maxy;

    if (camZ1 <= cameraDepth) continue; 

    float p1Y = segments[prev].y;
    float p2Y = seg.y;
    if (n == 0) {
      int pp = (prev - 1 + TOTAL_SEGS) % TOTAL_SEGS;
      p1Y = lerpF(segments[pp].y, segments[prev].y, basePct);
    }

    float sc1   = cameraDepth / camZ1;
    float cyp1  = p1Y - camY;
    float cxp1  = playerX * ROAD_W - curveX;
    int16_t sy1 = SCR_CY - (int)(sc1 * cyp1 * SCR_CY);
    int16_t sx1 = SCR_CX + (int)(sc1 * (-cxp1) * SCR_CX);
    int16_t sw1 = (int)(sc1 * ROAD_W * SCR_CX);

    float sc2   = (camZ2 > cameraDepth) ? cameraDepth / camZ2 : sc1;
    float cyp2  = p2Y - camY;
    float cxp2  = playerX * ROAD_W - curveX - curveDX;
    int16_t sy2 = SCR_CY - (int)(sc2 * cyp2 * SCR_CY);
    int16_t sx2 = SCR_CX + (int)(sc2 * (-cxp2) * SCR_CX);
    int16_t sw2 = (int)(sc2 * ROAD_W * SCR_CX);

    curveX  += curveDX;
    curveDX += seg.curve;

    rCache[n] = {sx1, sy1, sw1, sc1};

    if (sy1 <= sy2)  { rClip[n] = maxy; continue; } 
    if (sy2 >= maxy) { rClip[n] = maxy; continue; } 

    rClip[n] = maxy;

    int drawTop = max((int)sy2, 0);
    int drawBot = min((int)sy1, maxy);
    int bandH   = drawBot - drawTop;
    if (bandH <= 0) continue;

    float fogF = expFog((float)n / DRAW_DIST, FOG_DENSITY);
    bool isLight  = ((sIdx / RUMBLE_LEN) % 2) == 0;
    // Dentro del túnel: paredes oscuras en lugar de hierba
    uint16_t grassCol = lerpCol(isLight ? colGrassL : colGrassD, colFog, fogF);
    uint16_t wallCol  = isLight ? rgb(35, 32, 30) : rgb(28, 25, 22);
    uint16_t grass    = seg.tunnel ? wallCol : grassCol;
    uint16_t road   = lerpCol(isLight ? colRoadL   : colRoadD,   colFog, seg.tunnel ? 0 : fogF);
    uint16_t rumble = lerpCol(isLight ? colRumbleL : colRumbleD, colFog, seg.tunnel ? 0 : fogF);
    uint16_t lane   = lerpCol(colLane, colFog, seg.tunnel ? 0 : fogF);

    int subDiv = (bandH > 6) ? min(3, bandH / 3) : 1;
    for (int s = 0; s < subDiv; s++) {
      int subTop = drawTop + s * bandH / subDiv;
      int subBot = drawTop + (s + 1) * bandH / subDiv;
      int subH   = subBot - subTop;
      if (subH <= 0) continue;

      float tMid = (float)(2 * s + 1) / (float)(2 * subDiv);
      int cx  = (int)lerpF(sx2, sx1, tMid);
      int hw  = (int)lerpF(sw2, sw1, tMid);
      int rw  = max(1, hw / 6);
      int rdL = cx - hw,  rdR = cx + hw;
      int rmL = rdL - rw, rmR = rdR + rw;

      int e1 = max(0, min(rmL, SCR_W));
      if (e1 > 0) spr.fillRect(0, subTop, e1, subH, grass);

      int a2 = max(0, rmL), b2 = max(0, min(rdL, SCR_W));
      if (b2 > a2) spr.fillRect(a2, subTop, b2 - a2, subH, rumble);

      int a3 = max(0, rdL), b3 = min(SCR_W, rdR);
      if (b3 > a3) spr.fillRect(a3, subTop, b3 - a3, subH, road);

      int a4 = max(0, rdR), b4 = min(SCR_W, rmR);
      if (b4 > a4) spr.fillRect(a4, subTop, b4 - a4, subH, rumble);

      int s5 = max(0, min(rmR, SCR_W));
      if (s5 < SCR_W) spr.fillRect(s5, subTop, SCR_W - s5, subH, grass);
    }

    if (isLight && sw1 > 15 && bandH > 1) {
      int midX = (sx1 + sx2) / 2;
      int midW = (sw1 + sw2) / 2;
      int midY = drawTop + bandH / 2;
      int lh   = min(bandH, 3);
      for (int l = 1; l < LANES; l++) {
        float lt = (float)l / LANES;
        int lx = (int)lerpF(midX - midW, midX + midW, lt);
        int lw = max(1, midW / 30);
        spr.fillRect(lx - lw / 2, midY, lw, lh, lane);
      }
    }
    maxy = drawTop;
  }

  if (maxy > SCR_CY) {
    spr.fillRect(0, SCR_CY, SCR_W, maxy - SCR_CY, lerpCol(colGrassD, colFog, 0.7));
  }

  for (int n = DRAW_DIST - 1; n > 2; n--) {
    int sIdx = (baseIdx + n) % TOTAL_SEGS;
    Segment& seg = segments[sIdx];
    if (rCache[n].scale <= 0 || rCache[n].y >= SCR_H) continue;

    if (seg.spriteType >= 0) {
      int sprX = rCache[n].x + (int)(rCache[n].scale * seg.spriteOffset * ROAD_W * SCR_CX);
      drawSpriteShape(seg.spriteType, sprX, rCache[n].y, rCache[n].scale, rClip[n]);
    }

    for (int c = 0; c < MAX_CARS; c++) {
      if (findSegIdx(trafficCars[c].z) != sIdx) continue;
      int carX = rCache[n].x + (int)(rCache[n].scale * trafficCars[c].offset * ROAD_W * SCR_CX);
      drawTrafficCar(carX, rCache[n].y, rCache[n].scale, trafficCars[c].color, rClip[n]);
    }
  }
}

void drawHUD() {
  int kmh = (int)(speed * 300.0 / maxSpeed);

  spr.fillRect(0, 0, 130, 18, 0x0000);
  spr.setTextSize(1);
  spr.setTextColor(TFT_YELLOW, TFT_BLACK);
  spr.setCursor(2, 2);
  spr.print("SPD ");
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.print(kmh);
  spr.print(" km/h ");

  spr.setCursor(2, 11);
  spr.setTextColor(TFT_CYAN, TFT_BLACK);
  spr.print("LAP ");
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.print((int)currentLapTime);
  spr.print(".");
  spr.print((int)((currentLapTime - (int)currentLapTime) * 10));
  spr.print("s ");

  if (bestLapTime > 0 && bestLapTime < 999) {
    spr.setCursor(SCR_W - 82, 2);
    spr.setTextColor(TFT_GREEN, TFT_BLACK);
    spr.print("BEST ");
    spr.print((int)bestLapTime);
    spr.print(".");
    spr.print((int)((bestLapTime - (int)bestLapTime) * 10));
    spr.print(" ");
  }

  int barX = SCR_W - 105, barW = 100, barH = 5, barY = 13;
  int fill = (int)(speed / maxSpeed * barW);
  uint16_t bc = TFT_GREEN;
  if (speed > maxSpeed * 0.7) bc = TFT_YELLOW;
  if (speed > maxSpeed * 0.9) bc = TFT_RED;
  spr.drawRect(barX - 1, barY - 1, barW + 2, barH + 2, rgb(80, 80, 80));
  if (fill > 0)    spr.fillRect(barX, barY, fill, barH, bc);
  if (fill < barW) spr.fillRect(barX + fill, barY, barW - fill, barH, 0);
}

void handleInput(float dt) {
  bool left  = digitalRead(BTN_LEFT)  == LOW;
  bool right = digitalRead(BTN_RIGHT) == LOW;

  speed += (maxSpeed / 5.0) * dt;
  if (speed > maxSpeed) speed = maxSpeed;

  float steer = dt * 2.0 * (speed / maxSpeed);
  if (left)  playerX -= steer;
  if (right) playerX += steer;

  if ((playerX < -1.0 || playerX > 1.0) && speed > maxSpeed * 0.25)
    speed -= (maxSpeed / 2.0) * dt;

  speed   = clampF(speed,   0, maxSpeed);
  playerX = clampF(playerX, -2.5, 2.5);
}

void updatePhysics(float dt) {
  int pSeg = findSegIdx(position + playerZdist);
  float spPct = speed / maxSpeed;
  float steerDx = dt * 2.0 * spPct;
  playerX -= steerDx * spPct * segments[pSeg].curve * centrifugal;

  prevPosition = position;
  position = loopIncrease(position, dt * speed, trackLength);

  if (position < prevPosition && prevPosition > trackLength * 0.9) {
    if (currentLapTime > 5.0) {
      lastLapTime = currentLapTime;
      if (bestLapTime <= 0 || currentLapTime < bestLapTime)
        bestLapTime = currentLapTime;
    }
    currentLapTime = 0;
  }
  currentLapTime += dt;

  for (int i = 0; i < MAX_CARS; i++) {
    trafficCars[i].z = loopIncrease(trafficCars[i].z, dt * trafficCars[i].speed, trackLength);
    if (random(0, 200) < 2) {
      trafficCars[i].offset += (random(-1, 2)) * 0.1;
      trafficCars[i].offset  = clampF(trafficCars[i].offset, -0.8, 0.8);
    }
  }
}

void checkCollisions() {
  const float playerW = 0.15;
  int pSeg = findSegIdx(position + playerZdist);

  for (int i = 0; i < MAX_CARS; i++) {
    int cs = findSegIdx(trafficCars[i].z);
    int d  = abs(cs - pSeg);
    if (d > 3 && d < TOTAL_SEGS - 3) continue;
    if (speed > trafficCars[i].speed && overlapChk(playerX, playerW, trafficCars[i].offset, 0.15)) {
      speed = trafficCars[i].speed * 0.7;
      position = loopIncrease(position, -(speed * 0.05), trackLength);
      if (speed > maxSpeed * 0.5) { crashed = true; crashTimer = millis(); }
    }
  }

  if (playerX < -1.0 || playerX > 1.0) {
    Segment& s = segments[pSeg];
    if (s.spriteType >= 0 && overlapChk(playerX, playerW, s.spriteOffset, 0.4)) {
      speed *= 0.2;
      if (speed > maxSpeed * 0.25) { crashed = true; crashTimer = millis(); }
    }
  }

  if (playerX <= -2.4 || playerX >= 2.4) {
    speed *= 0.5;
    if (speed > maxSpeed * 0.4) { crashed = true; crashTimer = millis(); }
  }
}

void drawStartScreen() {
  spr.fillSprite(TFT_BLACK);
  spr.fillRect(0, 30, SCR_W, 3, TFT_RED);
  spr.fillRect(0, 35, SCR_W, 3, TFT_WHITE);
  spr.fillRect(0, 115, SCR_W, 3, TFT_WHITE);
  spr.fillRect(0, 120, SCR_W, 3, TFT_RED);

  spr.setTextColor(TFT_RED);
  spr.setTextSize(3);
  spr.setCursor(22, 50);
  spr.print("OUTRUN ESP32");

  spr.setTextSize(2);
  spr.setTextColor(TFT_YELLOW);
  spr.setCursor(60, 85);
  spr.print("3D RACING");

  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE);
  spr.setCursor(30, 140);
  spr.print("LEFT / RIGHT buttons to steer");
  spr.setCursor(30, 155);
  spr.print("Car accelerates automatically");
  spr.setCursor(30, 170);
  spr.print("Stay on road! Avoid traffic!");

  spr.setTextColor(TFT_CYAN);
  spr.setCursor(25, 195);
  spr.print("Hills + Curves + Fog + Traffic");

  spr.setTextColor(rgb(120, 120, 120));
  spr.setCursor(85, 220);
  spr.print("Get ready...");
  
  spr.pushSprite(0, 0); // Enviar el inicio a la pantalla
}

// ═══════════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  pinMode(BTN_LEFT,  INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  randomSeed(analogRead(0));

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  
  // ¡Inicializar el Sprite!
  spr.createSprite(SCR_W, SCR_H);

  float fovRad = FOV_DEG * PI / 180.0;
  cameraDepth  = 1.0 / tanf(fovRad / 2.0);
  playerZdist  = CAM_HEIGHT * cameraDepth;

  maxSpeed = SEG_LEN * 30.0;

  initColors();
  buildTrack();
  initTraffic();

  drawStartScreen();
  delay(2500);

  lastFrameMs = millis();
  distSinceTimeChange = 0;
}

// ═══════════════════════════════════════════════════════════════
//  MAIN LOOP
// ═══════════════════════════════════════════════════════════════
void loop() {
  unsigned long now = millis();
  float dt = (now - lastFrameMs) / 1000.0;
  lastFrameMs = now;
  if (dt > 0.1) dt = 0.1;

  if (!crashed) {
    handleInput(dt);
    updatePhysics(dt);
    checkCollisions();
  }

  // Dibujamos todo en el Sprite (en memoria RAM)
  drawSky();
  drawRoad();
  drawPlayerCar();
  drawHUD();

  if (crashed) {
    spr.fillRect(SCR_CX - 70, SCR_CY - 15, 140, 30, TFT_BLACK);
    spr.drawRect(SCR_CX - 71, SCR_CY - 16, 142, 32, TFT_RED);
    spr.setTextSize(3);
    spr.setTextColor(TFT_RED, TFT_BLACK);
    spr.setCursor(SCR_CX - 55, SCR_CY - 8);
    spr.print("CRASH!");
    if (millis() - crashTimer > 2000) {
      crashed = false;
      speed   = 0;
      playerX = 0;
    }
  }

  // ESTA ES LA MAGIA: Enviamos el frame completo a la pantalla
  spr.pushSprite(0, 0);

  distSinceTimeChange += (int)(speed * dt);
  if (distSinceTimeChange > 180000) {
    distSinceTimeChange = 0;
    timeOfDay = (timeOfDay + 1) % 3;
    initColors();
  }
}