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
#define TOTAL_SEGS  300       // Total de segmentos en la pista
#define ROAD_W      2000      // Ancho de la carretera
#define LANES       3         // Número de carriles
#define FOV_DEG     100       // Campo de visión en grados
#define CAM_HEIGHT  1000      // Altura de la cámara
#define FOG_DENSITY 5         // Densidad de la niebla

// ═══════════════════════════════════════════════════════════════
//  CONSTANTES DE TRÁFICO
// ═══════════════════════════════════════════════════════════════
#define MAX_CARS 6

#endif // CONFIG_H
