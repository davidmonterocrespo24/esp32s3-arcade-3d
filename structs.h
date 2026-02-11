/*
  ═══════════════════════════════════════════════════════════════
  ESTRUCTURAS DE DATOS DEL JUEGO
  ═══════════════════════════════════════════════════════════════
*/

#ifndef STRUCTS_H
#define STRUCTS_H

#include <Arduino.h>

// ═══════════════════════════════════════════════════════════════
//  ESTRUCTURA DE SEGMENTO DE PISTA
// ═══════════════════════════════════════════════════════════════
struct Segment {
  float curve;              // Curvatura del segmento
  float y;                  // Altura (elevación)
  int8_t spriteType;        // Tipo de sprite (-1 = ninguno)
  float  spriteOffset;      // Desplazamiento lateral del sprite

  // -- PROPIEDADES 3D POLIGONAL --
  bool   tunnel;            // true = dentro de túnel
  int    buildL, buildR;    // Altura edificio Izq/Der (0 = sin edificio)
  uint16_t colorL, colorR;  // Color fachada del edificio
};

// ═══════════════════════════════════════════════════════════════
//  PUNTO DE RENDERIZADO
// ═══════════════════════════════════════════════════════════════
struct RenderPt {
  int16_t x, y, w;          // Posición en pantalla y ancho
  float   scale;            // Factor de escala
};

// ═══════════════════════════════════════════════════════════════
//  COCHE DE TRÁFICO
// ═══════════════════════════════════════════════════════════════
struct TrafficCar {
  float    offset;          // Desplazamiento lateral (-1 a 1)
  float    z;               // Posición en la pista
  float    speed;           // Velocidad del coche
  uint16_t color;           // Color del coche
};

#endif // STRUCTS_H
