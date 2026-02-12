/*
  ═══════════════════════════════════════════════════════════════
  IMPLEMENTACIÓN DE RENDERIZADO DEL HUD Y VELOCÍMETRO
  ═══════════════════════════════════════════════════════════════
*/

#include "render_hud.h"
#include "rendering.h"
#include "config.h"
#include "colors.h"
#include "physics.h"

void drawSpeedometer(float speed, float maxSpeed) {
  // Posición inferior derecha
  int centerX = SCR_W - 55;
  int centerY = SCR_H - 55;
  int radius = 42;

  // Fondo semi-transparente
  spr.fillCircle(centerX, centerY, radius + 3, rgb(20, 20, 20));
  spr.drawCircle(centerX, centerY, radius + 3, TFT_DARKGREY);
  spr.drawCircle(centerX, centerY, radius + 2, rgb(60, 60, 60));

  // Calcular velocidad en km/h (0-300)
  int kmh = (int)(speed * 300.0 / maxSpeed);

  // Dibujar marcas del velocímetro (0, 100, 200, 300)
  for(int i = 0; i <= 6; i++) {
    float angle = (-225 + i * 75) * PI / 180.0;  // De -225° a 225° (270° total)
    int x1 = centerX + cos(angle) * (radius - 8);
    int y1 = centerY + sin(angle) * (radius - 8);
    int x2 = centerX + cos(angle) * (radius - 2);
    int y2 = centerY + sin(angle) * (radius - 2);

    uint16_t markColor = (i >= 5) ? TFT_RED : TFT_ORANGE;
    spr.drawLine(x1, y1, x2, y2, markColor);

    // Números cada 100 km/h
    if (i % 2 == 0) {
      int num = i * 50;
      int tx = centerX + cos(angle) * (radius - 18);
      int ty = centerY + sin(angle) * (radius - 18);
      spr.setTextSize(1);
      spr.setTextColor(TFT_WHITE, rgb(20, 20, 20));
      spr.setCursor(tx - 6, ty - 4);
      spr.print(num);
    }
  }

  // Aguja del velocímetro
  float needleAngle = -225 + (kmh / 300.0) * 270.0;  // Mapear 0-300 a -225/+45 grados
  needleAngle = needleAngle * PI / 180.0;

  int needleX = centerX + cos(needleAngle) * (radius - 10);
  int needleY = centerY + sin(needleAngle) * (radius - 10);

  // Sombra de la aguja
  spr.drawLine(centerX + 1, centerY + 1, needleX + 1, needleY + 1, rgb(10, 10, 10));

  // Aguja principal (más gruesa)
  uint16_t needleColor = (kmh > 250) ? TFT_RED : (kmh > 200) ? TFT_YELLOW : TFT_WHITE;
  spr.drawLine(centerX, centerY, needleX, needleY, needleColor);
  spr.drawLine(centerX - 1, centerY, needleX - 1, needleY, needleColor);
  spr.drawLine(centerX, centerY - 1, needleX, needleY - 1, needleColor);

  // Centro de la aguja
  spr.fillCircle(centerX, centerY, 4, needleColor);
  spr.drawCircle(centerX, centerY, 5, TFT_DARKGREY);

  // Texto de velocidad digital en el centro
  spr.setTextSize(2);
  spr.setTextColor(needleColor, rgb(20, 20, 20));
  spr.setCursor(centerX - 18, centerY + 12);
  if (kmh < 100) spr.print(" ");
  if (kmh < 10) spr.print(" ");
  spr.print(kmh);
}

void drawHUD(float speed, float maxSpeed, float currentLapTime, float bestLapTime) {
  int kmh = (int)(speed * 300.0 / maxSpeed);

  // === CONTADOR DE VUELTAS (Estilo Top Gear) ===
  spr.fillRect(0, 0, 90, 36, TFT_BLACK);
  spr.drawRect(0, 0, 90, 36, TFT_RED);
  spr.drawRect(1, 1, 88, 34, TFT_DARKGREY);

  // LAP X/3
  spr.setTextSize(2);
  spr.setTextColor(TFT_RED, TFT_BLACK);
  spr.setCursor(5, 4);
  spr.print("LAP ");
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.print(currentLap);
  spr.print("/");
  spr.print(totalLaps);

  // Tiempo actual
  spr.setTextSize(1);
  spr.setTextColor(TFT_YELLOW, TFT_BLACK);
  spr.setCursor(5, 22);
  spr.print("TIME ");
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  int mins = (int)currentLapTime / 60;
  int secs = (int)currentLapTime % 60;
  int decimals = (int)((currentLapTime - (int)currentLapTime) * 100);
  if (mins > 0) {
    spr.print(mins);
    spr.print(":");
    if (secs < 10) spr.print("0");
  }
  spr.print(secs);
  spr.print(".");
  if (decimals < 10) spr.print("0");
  spr.print(decimals);

  if (bestLapTime > 0 && bestLapTime < 999) {
    spr.setCursor(SCR_W - 82, 2);
    spr.setTextColor(TFT_GREEN, TFT_BLACK);
    spr.print("BEST ");
    spr.print((int)bestLapTime);
    spr.print(".");
    spr.print((int)((bestLapTime - (int)bestLapTime) * 10));
    spr.print(" ");
  }

  // Llamar al velocímetro circular
  drawSpeedometer(speed, maxSpeed);
}
