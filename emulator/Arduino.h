#ifndef Arduino_h
#define Arduino_h

// Minimal Arduino API mock for the PC builds (emulator + headless).

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <string>
#include <algorithm>

typedef uint8_t byte;
typedef bool boolean;

#define INPUT 0x0
#define OUTPUT 0x1
#define INPUT_PULLUP 0x2
#define HIGH 0x1
#define LOW 0x0
#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif

// Same behaviour as Arduino-ESP32 3.x: std templates, so mixed int/float
// calls fail to compile here too instead of only on the board.
using std::min;
using std::max;

unsigned long millis();
unsigned long micros();
void delay(unsigned long ms);
void randomSeed(unsigned long seed);
long random(long max);
long random(long min, long max);
int analogRead(uint8_t pin);
void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t val);
int digitalRead(uint8_t pin);

class SerialMock {
public:
  void begin(long) {}
  void print(const char* s) { fputs(s, stdout); }
  void println(const char* s) { puts(s); }
  void printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
  }
};
extern SerialMock Serial;

#define PROGMEM
#define pgm_read_byte(addr) (*(const unsigned char*)(addr))
#define pgm_read_word(addr) (*(const unsigned short*)(addr))

// Host clock control (headless runs use a virtual clock for determinism)
extern bool          g_emuVirtualClock;
extern unsigned long g_emuNowMs;

#endif
