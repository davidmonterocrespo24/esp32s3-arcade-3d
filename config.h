/*
  ═══════════════════════════════════════════════════════════════
  CONFIGURACIÓN Y CONSTANTES DEL JUEGO
  ═══════════════════════════════════════════════════════════════
*/

#ifndef CONFIG_H
#define CONFIG_H

// ═══════════════════════════════════════════════════════════════
//  HARDWARE PINS (Los de la pantalla van en User_Setup.h)
// ═══════════════════════════════════════════════════════════════
#define TFT_BL    39

// Botones
#define BTN_LEFT  17
#define BTN_RIGHT 16

// ═══════════════════════════════════════════════════════════════
//  CONSTANTES DE PANTALLA
// ═══════════════════════════════════════════════════════════════
#define SCR_W       320
#define SCR_H       240
#define SCR_CX      (SCR_W / 2)
#define SCR_CY      (SCR_H / 2)

// ═══════════════════════════════════════════════════════════════
//  CONSTANTES DE JUEGO
// ═══════════════════════════════════════════════════════════════
#define SEG_LEN     200       // Longitud de cada segmento
#define RUMBLE_LEN  3         // Longitud de las franjas de rumble
#define DRAW_DIST   40        // Distancia de dibujado
#define TOTAL_SEGS  200       // Total de segmentos en la pista (optimizado RAM)
#define ROAD_W      2000      // Ancho de la carretera
#define LANES       3         // Número de carriles
#define FOV_DEG     100       // Campo de visión en grados
#define CAM_HEIGHT  1000      // Altura de la cámara
#define FOG_DENSITY 5         // Densidad de la niebla

// 1 = pista aleatoria al iniciar, 0 = pista fija
#define RANDOM_TRACK 1

// ═══════════════════════════════════════════════════════════════
//  FÍSICA DEL AUTO
// ═══════════════════════════════════════════════════════════════
#define SPEED_MULTIPLIER   65.0f   // maxSpeed = SEG_LEN * SPEED_MULTIPLIER (~246 km/h)
#define ACCEL_TARGET       0.9f    // targetAccel = maxSpeed * ACCEL_TARGET
#define ACCEL_RAMP         180.0f  // Qué tan rápido sube la aceleración (u/s²)
#define ACCEL_NEAR_MAX     0.90f   // Umbral de velocidad para reducir aceleración
#define ACCEL_DAMPING      0.97f   // Multiplicador al acercarse a maxSpeed
#define FRICTION           0.996f  // Fricción por frame (frenado ~3.3s desde max)
#define GRAVITY_FACTOR     1600.0f // Efecto de pendientes en aceleración
#define CENTRIFUGAL        0.18f   // Fuerza centrífuga en curvas
#define CURVE_FORCE        3.0f    // Multiplicador de fuerza lateral en curvas
#define LATERAL_FRICTION   0.90f   // Amortiguación de velocidad lateral por frame
#define STEER_AUTO         1.8f    // Respuesta del autopiloto a las curvas
#define CENTRIFUGAL_DX     1.5f    // Factor de empuje centrífugo lateral

// ═══════════════════════════════════════════════════════════════
//  EDIFICIOS
// ═══════════════════════════════════════════════════════════════
#define BUILDING_H_MIN     120000  // Altura mínima de edificios (~10 pisos)
#define BUILDING_H_MAX     350000  // Altura máxima de edificios (~30 pisos)
#define BUILDING_W         400000  // Ancho base de edificio
#define BUILDING_OFFSET    1.5f    // Distancia desde el borde de la carretera
#define BUILDING_SEG_MIN   6       // Segmentos mínimos por bloque
#define BUILDING_SEG_MAX   16      // Segmentos máximos por bloque
#define BUILDING_GAP_MIN   10      // Segmentos mínimos de hueco entre bloques
#define BUILDING_GAP_MAX   20      // Segmentos máximos de hueco entre bloques

// ═══════════════════════════════════════════════════════════════
//  TRÁFICO
// ═══════════════════════════════════════════════════════════════
#define MAX_CARS 6

#endif // CONFIG_H
