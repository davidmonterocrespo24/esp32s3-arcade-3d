/*
  ═══════════════════════════════════════════════════════════════
  IMPLEMENTACIÓN DE GENERACIÓN DE PISTA
  ═══════════════════════════════════════════════════════════════
*/

#include "track.h"
#include "utils.h"
#include "colors.h"
#include <Arduino.h>

// ═══════════════════════════════════════════════════════════════
//  VARIABLES GLOBALES DE PISTA (Definición)
// ═══════════════════════════════════════════════════════════════
Segment segments[TOTAL_SEGS];
int segCount = 0;
float trackLength;

TrafficCar trafficCars[MAX_CARS];

// ═══════════════════════════════════════════════════════════════
//  IMPLEMENTACIÓN
// ═══════════════════════════════════════════════════════════════

float lastY() {
  return (segCount == 0) ? 0 : segments[(segCount - 1) % TOTAL_SEGS].y;
}

void addSeg(float curve, float y, bool isTunnel) {
  if (segCount >= TOTAL_SEGS) return;
  segments[segCount].curve        = curve;
  segments[segCount].y            = y;
  segments[segCount].spriteType   = -1;
  segments[segCount].spriteOffset = 0;
  segments[segCount].tunnel       = isTunnel;
  segments[segCount].buildL       = 0;
  segments[segCount].buildR       = 0;
  segments[segCount].colorL       = 0;
  segments[segCount].colorR       = 0;
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

void buildTrack() {
  segCount = 0;

  // Agregar curvas normales
  addRoad(25, 25, 25, 0, 0);      // Recta de salida
  addRoad(50, 50, 50, -2.0, 0);   // Curva izquierda
  addRoad(50, 50, 50,  4.0, 40);  // Curva derecha + colina
  addRoad(50, 50, 50,  2.0, -20); // Curva suave + bajada
  addRoad(50, 50, 50, -4.0, -40); // Curva cerrada izq + bajada

  // Rellenar hasta TOTAL_SEGS
  while (segCount < TOTAL_SEGS) addSeg(0, 0);
  trackLength = (float)TOTAL_SEGS * SEG_LEN;

  // 1. ZONA DEL TÚNEL (Segmentos 120 al 180)
  for (int i = 120; i < 180; i++) {
    segments[i].tunnel = true;
  }

  // 2. CONSTRUIR LA CIUDAD (Agrupar edificios en bloques de manzanas)
  int curBuildL = 0, curBuildR = 0;
  uint16_t curColL = 0, curColR = 0;

  for (int i = 0; i < TOTAL_SEGS; i++) {
    if (segments[i].tunnel) continue;

    // Cada 12 segmentos creamos una "manzana" nueva
    if (i % 12 == 0) {
      curBuildL = (random(0, 2) == 0) ? random(60000, 150000) : 0;
      curBuildR = (random(0, 2) == 0) ? random(60000, 150000) : 0;
      curColL = rgb(random(60, 150), random(60, 150), random(80, 180));
      curColR = rgb(random(60, 150), random(60, 150), random(80, 180));
    }

    // Los primeros 9 segmentos son el edificio, los últimos 3 son el hueco (calle lateral)
    if (i % 12 < 9) {
      segments[i].buildL = curBuildL;
      segments[i].buildR = curBuildR;
      segments[i].colorL = curColL;
      segments[i].colorR = curColR;
    }
  }

  // 3. Árboles en huecos entre edificios
  for (int n = 5; n < segCount; n++) {
    if (segments[n].tunnel || segments[n].buildL > 0 || segments[n].buildR > 0) continue;
    int r = random(0, 100);
    if (r < 10) addSprite(n, random(0, 3), -1.5);
    else if (r < 20) addSprite(n, random(0, 3), 1.5);
  }
}

// Colores de tráfico en Flash (PROGMEM) - ahorra RAM
const uint16_t PROGMEM trafficColors[] = {
  0x051C,  // rgb(0,80,220)   - Azul
  0xDDE0,  // rgb(220,200,0)  - Amarillo
  0xC618,  // rgb(200,200,200)- Gris
  0x05A0,  // rgb(0,180,80)   - Verde
  0xFC60,  // rgb(255,100,0)  - Naranja
  0xA01C,  // rgb(160,0,200)  - Morado
  0x05BC,  // rgb(0,180,180)  - Cyan
  0xB1E8,  // rgb(180,60,60)  - Rojo oscuro
  0xFDB2,  // rgb(255,180,200)- Rosa
  0x6318,  // rgb(100,100,100)- Gris oscuro
  0x0780,  // rgb(0,120,0)    - Verde oscuro
  0xC5E0   // rgb(200,150,0)  - Dorado
};

void initTraffic(float maxSpeed) {
  for (int i = 0; i < MAX_CARS; i++) {
    trafficCars[i].offset = random(-8, 9) / 10.0;
    trafficCars[i].z      = random(0, TOTAL_SEGS) * SEG_LEN;
    trafficCars[i].speed  = maxSpeed * (0.2 + random(0, 50) / 100.0);
    trafficCars[i].color  = pgm_read_word(&trafficColors[i % 12]);
  }
}
