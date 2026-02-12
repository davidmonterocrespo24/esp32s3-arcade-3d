#include <chrono>
#include <thread>
#include "raylib.h"
#include "../config.h"
#include "SPI.h"
#include "Arduino.h"

// Global objects
SerialMock Serial;
ESPMock ESP;
SPIClass SPI;

// Chrono for millis
auto start_time = std::chrono::steady_clock::now();

unsigned long millis() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
}

void delay(unsigned long ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void randomSeed(long seed) {
    SetRandomSeed(seed);
}

int random(int max) {
    if (max == 0) return 0;
    return GetRandomValue(0, max - 1);
}

int random(int min, int max) {
    if (min >= max) return min;
    return GetRandomValue(min, max - 1);
}

int analogRead(uint8_t pin) {
    return 0; // Dummy
}

void pinMode(uint8_t pin, uint8_t mode) {
    // No-op
}

void digitalWrite(uint8_t pin, uint8_t val) {
    // No-op
}

// Input handling mapping
// config.h defines BTN_LEFT 17, BTN_RIGHT 16
// INPUT_PULLUP: LOW is pressed, HIGH is released.
int digitalRead(uint8_t pin) {
    if (pin == BTN_LEFT) {
        return IsKeyDown(KEY_LEFT) ? LOW : HIGH;
    }
    if (pin == BTN_RIGHT) {
        return IsKeyDown(KEY_RIGHT) ? LOW : HIGH;
    }
    return HIGH;
}
