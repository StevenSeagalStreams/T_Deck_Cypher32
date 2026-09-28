#pragma once
// Host stub — the slice of Arduino's Print that Adafruit_GFX and the T-Deck
// UI use. Everything funnels into write(uint8_t), as on the device.
#include <Arduino.h>
#include <cstdarg>
class __FlashStringHelper;
#ifndef radians
#define radians(d) ((d) * 0.017453292519943295)
#endif
#ifndef PI
#define PI 3.14159265358979323846
#endif
class Print {
public:
  virtual ~Print() {}
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t* b, size_t n) { size_t k = 0; while (n--) k += write(*b++); return k; }
  size_t write(const char* s) { return s ? write((const uint8_t*)s, strlen(s)) : 0; }
  size_t print(const char* s)   { return write(s); }
  size_t print(const String& s) { return write(s.c_str()); }
  size_t print(char c)          { return write((uint8_t)c); }
  size_t print(int v)           { char b[16]; snprintf(b, sizeof b, "%d", v); return write(b); }
  size_t print(unsigned v)      { char b[16]; snprintf(b, sizeof b, "%u", v); return write(b); }
  size_t print(long v)          { char b[24]; snprintf(b, sizeof b, "%ld", v); return write(b); }
  size_t print(unsigned long v) { char b[24]; snprintf(b, sizeof b, "%lu", v); return write(b); }
  size_t print(double v, int d = 2) { char b[32]; snprintf(b, sizeof b, "%.*f", d, v); return write(b); }
  size_t println()              { return write("\n"); }
  template <typename T> size_t println(const T& v) { size_t n = print(v); return n + println(); }
  size_t printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    char b[256]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap); return write(b);
  }
};
