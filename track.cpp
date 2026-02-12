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

  // Circuito muy variado con curvas pronunciadas en ambos sentidos y muchas colinas
  addRoad(20, 30, 20, 0, 0);         // Recta de salida
  addRoad(30, 50, 30,  6.0, 15);     // Curva DERECHA pronunciada + subida
  addRoad(25, 30, 25,  0, -25);      // Recta + bajada fuerte
  addRoad(40, 60, 40, -7.0, 0);      // Curva IZQUIERDA muy pronunciada
  addRoad(20, 25, 20,  0, 30);       // Recta + colina grande
  addRoad(35, 45, 35,  5.5, -20);    // Curva DERECHA fuerte + bajada
  addRoad(25, 35, 25, -6.5, 10);     // Curva IZQUIERDA pronunciada + subida
  addRoad(30, 40, 30,  0, -15);      // Recta + bajada
  addRoad(40, 50, 40,  7.5, 20);     // Curva DERECHA extrema + colina
  addRoad(30, 40, 30, -5.0, -25);    // Curva IZQUIERDA fuerte + bajada
  addRoad(25, 30, 25,  4.5, 0);      // Curva DERECHA media
  addRoad(35, 45, 35, -6.0, 15);     // Curva IZQUIERDA pronunciada + subida
  addRoad(20, 30, 20,  0, -20);      // Recta + bajada
  addRoad(40, 55, 40,  6.5, 10);     // Curva DERECHA fuerte + subida
  addRoad(30, 35, 30, -4.5, -10);    // Curva IZQUIERDA media + bajada

  // Rellenar hasta TOTAL_SEGS
  while (segCount < TOTAL_SEGS) addSeg(0, 0, false);
  trackLength = (float)TOTAL_SEGS * SEG_LEN;

  // 1. TÚNEL ÚNICO (Solo 1 túnel largo, no múltiples)
  // Posicionado en el segundo tercio de la pista
  // EXTENSION: 60 segmentos (mas largo)
  for (int i = 50; i < 130; i++) { 
    segments[i].tunnel = true;
    segments[i].buildL = 0; // Sin edificios dentro
    segments[i].buildR = 0;
  }

  // 2. CONSTRUIR LA CIUDAD (Edificios variados estilo Nueva York/Horizon Chase)
  int buildCounterL = 0; // Contadores para duración del edificio actual
  int buildCounterR = 0;
  int curBuildL = 0, curBuildR = 0;
  uint16_t curColL = 0, curColR = 0;

  for (int i = 0; i < TOTAL_SEGS; i++) {
    if (segments[i].tunnel) continue;

    // --- LADO IZQUIERDO (Edificios estilo ciudad) ---
    if (buildCounterL <= 0) {
      // 50% probabilidad de edificio, 50% hueco (Menos juntos: "no tan juntos")
      if (random(0, 10) < 5) {
        // Altura EXTREMA (Rascacielos)
        curBuildL = random(400000, 1000000); 
        
        // Colores más variados y urbanos
        curColL = rgb(random(40, 140), random(40, 120), random(50, 130));
        
        // MUCHO MÁS ANCHOS (Segmentos)
        buildCounterL = random(20, 45);
      } else {
        curBuildL = 0; // Hueco entre edificios
        buildCounterL = random(3, 8); // Huecos más largos también
      }
    }
    segments[i].buildL = curBuildL;
    segments[i].colorL = curColL;
    buildCounterL--;

    // --- LADO DERECHO (Lógica independiente) ---
    if (buildCounterR <= 0) {
      if (random(0, 10) < 6) { 
        curBuildR = random(400000, 1000000); 
        curColR = rgb(random(40, 140), random(40, 120), random(50, 130));
        buildCounterR = random(20, 45); // Más segmentos
      } else {
        curBuildR = 0;
        buildCounterR = random(3, 8);
      }
    }
    segments[i].buildR = curBuildR;
    segments[i].colorR = curColR;
    buildCounterR--;
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
