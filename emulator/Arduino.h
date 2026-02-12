#ifndef Arduino_h
#define Arduino_h

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <iostream>
#include <string>
#include <algorithm>

// Types
typedef uint8_t byte;
typedef bool boolean;
using std::string;
#define String std::string

// Constants
#define INPUT 0x0
#define OUTPUT 0x1
#define INPUT_PULLUP 0x2
#define HIGH 0x1
#define LOW 0x0
#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif

// Functions
extern unsigned long millis();
extern void delay(unsigned long ms);
extern void randomSeed(long seed);
extern int random(int max);
extern int random(int min, int max);
extern int analogRead(uint8_t pin);
extern void pinMode(uint8_t pin, uint8_t mode);
extern void digitalWrite(uint8_t pin, uint8_t val);

// Math
#ifndef min
#define min(a,b) ((a)<(b)?(a):(b))
#endif
#ifndef max
#define max(a,b) ((a)>(b)?(a):(b))
#endif
#define abs(x) ((x)>0?(x):-(x))
#define constrain(amt,low,high) ((amt)<(low)?(low):((amt)>(high)?(high):(amt)))

// Serial Mock
class SerialMock {
public:
    void begin(long baud) { printf("Serial started at %ld\n", baud); }
    void println(const char* s) { printf("%s\n", s); }
    void println(String s) { printf("%s\n", s.c_str()); }
    void println(long n) { printf("%ld\n", n); }
    void println(unsigned long n) { printf("%lu\n", n); }
    void println(uint32_t n) { printf("%u\n", n); }
    void println(int n) { printf("%d\n", n); }
    void println(float n) { printf("%f\n", n); }
    void print(long n) { printf("%ld", n); }
    void print(unsigned long n) { printf("%lu", n); }
    void print(const char* s) { printf("%s", s); }
    void print(String s) { printf("%s", s.c_str()); }
    void print(int n) { printf("%d", n); }
    void print(float n) { printf("%f", n); }
};
extern SerialMock Serial;

// PROGMEM Mocks
#define PROGMEM
#define pgm_read_byte(addr) (*(const unsigned char *)(addr))
#define pgm_read_word(addr) (*(const unsigned short *)(addr))

// ESP / PSRAM Mocks
class ESPMock {
public:
    uint32_t getPsramSize() { return 8 * 1024 * 1024; }
    uint32_t getFreePsram() { return 4 * 1024 * 1024; }
};
extern ESPMock ESP;

#endif
