/*
  ═══════════════════════════════════════════════════════════════
  UTILITY FUNCTIONS
  ═══════════════════════════════════════════════════════════════
*/

#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>

// ═══════════════════════════════════════════════════════════════
//  MATH FUNCTIONS
// ═══════════════════════════════════════════════════════════════

// Clamp a value between a minimum and maximum
float clampF(float v, float lo, float hi);

// Linear interpolation
float lerpF(float a, float b, float t);

// Ease-in interpolation
float easeIn(float a, float b, float t);

// Ease-in-out interpolation
float easeInOut(float a, float b, float t);

// Increment a value with wrap-around
float loopIncrease(float v, float inc, float mx);

// Calculate remaining percentage in a cycle
float percentRemaining(float v, float total);

// Find segment index by Z position
int findSegIdx(float z);

// Calculate exponential fog
float expFog(float d, float density);

// Check overlap between two objects
bool overlapChk(float x1, float w1, float x2, float w2);

#endif // UTILS_H
