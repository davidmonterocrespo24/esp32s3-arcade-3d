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

#if RANDOM_TRACK
  // Pista aleatoria: combina rectas, curvas y subidas/bajadas
  int pendingReturnDir = 0;
  float pendingReturnMag = 0.0f;
  // Reservar segmentos al final para la sección niveladora de cierre
  const int CLOSE_SEGS = 20;
  while (segCount < TOTAL_SEGS - CLOSE_SEGS) {
    int enter = random(4, 8);
    int hold  = random(6, 14);
    int leave = random(4, 8);
    int needed = enter + hold + leave;

    // Si no hay espacio suficiente para esta sección completa, parar aquí
    if (segCount + needed > TOTAL_SEGS - CLOSE_SEGS) break;

    float curve = (float)random(-80, 81) / 10.0f; // -8.0 a 8.0
    if (curve > -2.0f && curve < 2.0f) curve = 0.0f;

    float hill = 0.0f;
    if (pendingReturnDir != 0) {
      hill = (float)pendingReturnDir * pendingReturnMag;
      pendingReturnDir = 0;
    } else {
      // Limitar hillY para que el track no acumule alturas extremas
      float currentY = lastY();
      float maxAllowedHill = 8.0f; // Máximo delta por sección
      if (fabsf(currentY) > SEG_LEN * 4) {
        // Si ya estamos muy alto/bajo, forzar regreso
        hill = (currentY > 0) ? -maxAllowedHill : maxAllowedHill;
      } else {
        hill = (float)random(-12, 13); // rango reducido: -12 a 12
        if (hill > -6.0f && hill < 6.0f) hill = 0.0f;
        if (hill != 0.0f) {
          pendingReturnDir = (hill > 0.0f) ? -1 : 1;
          pendingReturnMag = max(6.0f, fabsf(hill) * 0.6f);
        }
      }
    }

    addRoad(enter, hold, leave, curve, hill);
  }

  // Sección de cierre: nivelar el Y de regreso a 0 para que el loop sea coherente
  {
    float currentY = lastY();
    if (fabsf(currentY) > SEG_LEN * 0.5f) {
      // Calcular hillY necesario para regresar a 0
      int closeSegsLeft = TOTAL_SEGS - CLOSE_SEGS - segCount;
      int enter = max(4, closeSegsLeft / 3);
      int hold  = 2;
      int leave = max(4, closeSegsLeft / 3);
      // hillY tal que sY + hillY*SEG_LEN = 0 → hillY = -sY/SEG_LEN
      float hillY = -currentY / (float)SEG_LEN;
      // Limitar magnitud
      hillY = max(-14.0f, min(14.0f, hillY));
      addRoad(enter, hold, leave, 0.0f, hillY);
    }
  }
#else
  // Circuito variado que alterna curvas izq/der y sube/baja constantemente
  // TOTAL aprox: 15 secciones × ~13 segs = ~195 segmentos
  addRoad(5, 10, 5, 0, 0);           // 1. Recta de salida
  addRoad(8, 12, 8, -6.0, 10);       // 2. IZQUIERDA + subida
  addRoad(5, 8, 5, 0, -15);          // 3. Recta + bajada
  addRoad(8, 12, 8, 7.0, 0);         // 4. DERECHA fuerte
  addRoad(5, 8, 5, 0, 20);           // 5. Recta + colina
  addRoad(8, 12, 8, -5.5, -10);      // 6. IZQUIERDA + bajada
  addRoad(5, 8, 5, 0, 0);            // 7. Recta plana
  addRoad(8, 12, 8, 6.5, 15);        // 8. DERECHA + subida
  addRoad(5, 8, 5, 0, -20);          // 9. Recta + bajada
  addRoad(8, 12, 8, -7.5, 0);        // 10. IZQUIERDA extrema
  addRoad(5, 8, 5, 0, 10);           // 11. Recta + subida
  addRoad(8, 12, 8, 5.0, -15);       // 12. DERECHA + bajada
  addRoad(5, 8, 5, 0, 0);            // 13. Recta
  addRoad(8, 12, 8, -6.5, 20);       // 14. IZQUIERDA + colina
  addRoad(5, 10, 5, 0, -10);         // 15. Recta final + bajada
#endif

  // Rellenar hasta TOTAL_SEGS
  while (segCount < TOTAL_SEGS) addSeg(0, 0, false);
  trackLength = (float)TOTAL_SEGS * SEG_LEN;

  // 1. TÚNEL ÚNICO (Solo 1 túnel largo, no múltiples)
  // Posicionado en el segundo tercio de la pista
  // EXTENSION: 60 segmentos (mas largo)
  int tunnelStart = TOTAL_SEGS / 3;
  int tunnelLen = min(60, TOTAL_SEGS - tunnelStart - 1);
  for (int i = tunnelStart; i < tunnelStart + tunnelLen; i++) {
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
      if (random(0, 10) < 6) {
        curBuildL = random(BUILDING_H_MIN, BUILDING_H_MAX);
        curColL = rgb(random(40, 140), random(40, 120), random(50, 130));
        buildCounterL = random(BUILDING_SEG_MIN, BUILDING_SEG_MAX);
      } else {
        curBuildL = 0;
        buildCounterL = random(BUILDING_GAP_MIN, BUILDING_GAP_MAX);
      }
    }
    segments[i].buildL = curBuildL;
    segments[i].colorL = curColL;
    buildCounterL--;

    // --- LADO DERECHO (Lógica independiente) ---
    if (buildCounterR <= 0) {
      if (random(0, 10) < 6) {
        curBuildR = random(BUILDING_H_MIN, BUILDING_H_MAX);
        curColR = rgb(random(40, 140), random(40, 120), random(50, 130));
        buildCounterR = random(BUILDING_SEG_MIN, BUILDING_SEG_MAX);
      } else {
        curBuildR = 0;
        buildCounterR = random(BUILDING_GAP_MIN, BUILDING_GAP_MAX);
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
