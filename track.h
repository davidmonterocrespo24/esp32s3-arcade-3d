/*
  ═══════════════════════════════════════════════════════════════
  GENERACIÓN Y GESTIÓN DE PISTA
  ═══════════════════════════════════════════════════════════════
*/

#ifndef TRACK_H
#define TRACK_H

#include "structs.h"
#include "config.h"

// ═══════════════════════════════════════════════════════════════
//  VARIABLES GLOBALES DE PISTA
// ═══════════════════════════════════════════════════════════════
extern Segment segments[TOTAL_SEGS];
extern int segCount;
extern float trackLength;

// ═══════════════════════════════════════════════════════════════
//  FUNCIONES DE PISTA
// ═══════════════════════════════════════════════════════════════

// Obtener la altura del último segmento
float lastY();

// Agregar un segmento a la pista
void addSeg(float curve, float y, bool isTunnel = false);

// Agregar una sección de carretera con curvas y elevación
void addRoad(int enter, int hold, int leave, float curve, float hillY);

// Agregar un sprite a un segmento específico
void addSprite(int idx, int type, float off);

// Construir la pista completa
void buildTrack();

// ═══════════════════════════════════════════════════════════════
//  GESTIÓN DE TRÁFICO
// ═══════════════════════════════════════════════════════════════
extern TrafficCar trafficCars[MAX_CARS];

// Inicializar coches de tráfico
void initTraffic(float maxSpeed);

#endif // TRACK_H
