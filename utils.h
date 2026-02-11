/*
  ═══════════════════════════════════════════════════════════════
  FUNCIONES UTILITARIAS
  ═══════════════════════════════════════════════════════════════
*/

#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>

// ═══════════════════════════════════════════════════════════════
//  FUNCIONES MATEMÁTICAS
// ═══════════════════════════════════════════════════════════════

// Limitar valor entre un mínimo y máximo
float clampF(float v, float lo, float hi);

// Interpolación lineal
float lerpF(float a, float b, float t);

// Interpolación con ease-in
float easeIn(float a, float b, float t);

// Interpolación con ease-in-out
float easeInOut(float a, float b, float t);

// Incrementar valor con wrap-around
float loopIncrease(float v, float inc, float mx);

// Calcular porcentaje restante en un ciclo
float percentRemaining(float v, float total);

// Encontrar índice de segmento según posición Z
int findSegIdx(float z);

// Calcular niebla exponencial
float expFog(float d, float density);

// Verificar superposición entre dos objetos
bool overlapChk(float x1, float w1, float x2, float w2);

#endif // UTILS_H
