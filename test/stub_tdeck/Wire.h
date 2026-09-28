#pragma once
// Host stub — the T-Deck keyboard is an I2C device that hands over one byte
// per keypress. Tests push keys into g_keyQueue; requestFrom() pops one.
#include <Arduino.h>
#include <deque>
extern std::deque<uint8_t> g_keyQueue;
struct TwoWire {
  int pending = -1;
  bool begin(int = -1, int = -1, uint32_t = 0) { return true; }
  void setClock(uint32_t) {}
  uint8_t requestFrom(uint8_t, uint8_t n) {
    // A real I2C read takes about a millisecond. Time passing here, in the
    // middle of a frame, is what exposed the screen blanking on a keypress.
    extern uint32_t g_millis; g_millis += 1;
    pending = g_keyQueue.empty() ? 0 : g_keyQueue.front();
    if (!g_keyQueue.empty()) g_keyQueue.pop_front();
    return n ? 1 : 0;
  }
  int  available() { return pending >= 0 ? 1 : 0; }
  int  read() { int v = pending; pending = -1; return v; }
  void beginTransmission(uint8_t) {}
  size_t write(uint8_t) { return 1; }
  uint8_t endTransmission(bool = true) { return 0; }
};
extern TwoWire Wire;
