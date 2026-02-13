/*
  ═══════════════════════════════════════════════════════════════
  HUD AND SPEEDOMETER RENDERING IMPLEMENTATION
  ═══════════════════════════════════════════════════════════════
*/

#include "render_hud.h"
#include "rendering.h"
#include "config.h"
#include "colors.h"
#include "physics.h"
#include "track.h"

void drawSpeedometer(float speed, float maxSpeed) {
  // Bottom-right position
  int centerX = SCR_W - 55;
  int centerY = SCR_H - 55;
  int radius = 42;

  // Semi-transparent background
  spr.fillCircle(centerX, centerY, radius + 3, rgb(20, 20, 20));
  spr.drawCircle(centerX, centerY, radius + 3, TFT_DARKGREY);
  spr.drawCircle(centerX, centerY, radius + 2, rgb(60, 60, 60));

  // Calculate speed in km/h (0-300)
  int kmh = (int)(speed * 300.0 / maxSpeed);

  // Draw speedometer marks (0, 100, 200, 300)
  for(int i = 0; i <= 6; i++) {
    float angle = (-225 + i * 75) * PI / 180.0;  // From -225° to 225° (270° total)
    int x1 = centerX + cos(angle) * (radius - 8);
    int y1 = centerY + sin(angle) * (radius - 8);
    int x2 = centerX + cos(angle) * (radius - 2);
    int y2 = centerY + sin(angle) * (radius - 2);

    uint16_t markColor = (i >= 5) ? TFT_RED : TFT_ORANGE;
    spr.drawLine(x1, y1, x2, y2, markColor);

    // Numbers every 100 km/h
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

  // Speedometer needle
  float needleAngle = -225 + (kmh / 300.0) * 270.0;  // Map 0-300 to -225/+45 degrees
  needleAngle = needleAngle * PI / 180.0;

  int needleX = centerX + cos(needleAngle) * (radius - 10);
  int needleY = centerY + sin(needleAngle) * (radius - 10);

  // Needle shadow
  spr.drawLine(centerX + 1, centerY + 1, needleX + 1, needleY + 1, rgb(10, 10, 10));

  // Main needle (thicker)
  uint16_t needleColor = (kmh > 250) ? TFT_RED : (kmh > 200) ? TFT_YELLOW : TFT_WHITE;
  spr.drawLine(centerX, centerY, needleX, needleY, needleColor);
  spr.drawLine(centerX - 1, centerY, needleX - 1, needleY, needleColor);
  spr.drawLine(centerX, centerY - 1, needleX, needleY - 1, needleColor);

  // Needle center
  spr.fillCircle(centerX, centerY, 4, needleColor);
  spr.drawCircle(centerX, centerY, 5, TFT_DARKGREY);

  // Digital speed text in the center
  spr.setTextSize(2);
  spr.setTextColor(needleColor, rgb(20, 20, 20));
  spr.setCursor(centerX - 18, centerY + 12);
  if (kmh < 100) spr.print(" ");
  if (kmh < 10) spr.print(" ");
  spr.print(kmh);
}

void drawCountdown() {
  unsigned long elapsed = millis() - countdownStart;
  // Countdown phases: 0-1000ms=3, 1000-2000ms=2, 2000-3000ms=1, 3000-3600ms=GO
  int cx = SCR_CX;
  int cy = SCR_CY - 20;

  if (elapsed < 3600) {
    // Background box
    spr.fillRect(cx - 40, cy - 30, 80, 60, rgb(0, 0, 0));
    spr.drawRect(cx - 40, cy - 30, 80, 60, TFT_WHITE);

    spr.setTextSize(4);
    if (elapsed < 1000) {
      spr.setTextColor(TFT_RED, TFT_BLACK);
      spr.setCursor(cx - 12, cy - 16);
      spr.print("3");
    } else if (elapsed < 2000) {
      spr.setTextColor(TFT_YELLOW, TFT_BLACK);
      spr.setCursor(cx - 12, cy - 16);
      spr.print("2");
    } else if (elapsed < 3000) {
      spr.setTextColor(TFT_GREEN, TFT_BLACK);
      spr.setCursor(cx - 12, cy - 16);
      spr.print("1");
    } else {
      // GO!
      spr.fillRect(cx - 52, cy - 20, 104, 40, TFT_GREEN);
      spr.setTextColor(TFT_WHITE, TFT_GREEN);
      spr.setTextSize(3);
      spr.setCursor(cx - 24, cy - 10);
      spr.print("GO!");
    }
  }
}

void drawRaceResults() {
  // Determine player position by comparing laps and track position
  int playerPos = 1;
  for (int i = 0; i < NUM_COMPETITORS; i++) {
    bool compAhead = (competitors[i].lap > currentLap) ||
                     (competitors[i].lap == currentLap && competitors[i].z > position);
    if (compAhead) playerPos++;
  }

  // Podium box
  spr.fillRect(SCR_CX - 90, SCR_CY - 55, 180, 110, TFT_BLACK);
  spr.drawRect(SCR_CX - 90, SCR_CY - 55, 180, 110, TFT_YELLOW);
  spr.drawRect(SCR_CX - 89, SCR_CY - 54, 178, 108, rgb(80, 70, 0));

  spr.setTextSize(2);
  spr.setTextColor(TFT_YELLOW, TFT_BLACK);
  spr.setCursor(SCR_CX - 70, SCR_CY - 45);
  spr.print("RACE FINISH!");

  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.setCursor(SCR_CX - 80, SCR_CY - 22);
  spr.print("YOUR POSITION: ");
  spr.setTextSize(3);
  uint16_t posCol = (playerPos == 1) ? TFT_YELLOW : (playerPos == 2) ? TFT_WHITE : TFT_ORANGE;
  static const char* posStr[] = { "1ST", "2ND", "3RD", "4TH" };
  spr.setTextColor(posCol, TFT_BLACK);
  spr.setCursor(SCR_CX - 30, SCR_CY - 10);
  spr.print(posStr[playerPos - 1]);

  spr.setTextSize(1);
  spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
  spr.setCursor(SCR_CX - 55, SCR_CY + 30);
  spr.print("Restarting in 3 sec...");
}

void drawHUD(float speed, float maxSpeed, float currentLapTime, float bestLapTime) {
  int kmh = (int)(speed * 300.0 / maxSpeed);

  // === LAP COUNTER (Top Gear style) ===
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

  // Current time
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

  // Call circular speedometer
  drawSpeedometer(speed, maxSpeed);
}
