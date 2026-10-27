#pragma once
// Host stub — the T-Deck's I2C bus.
//
//  0x55  the keyboard: one byte per keypress. Tests push keys into
//        g_keyQueue; requestFrom() pops one.
//  0x5D  the GT911 touch panel, when g_touch.present: a register file just
//        big enough for what tdeck_touch.h reads (product id, status, the
//        first finger). Tests set a finger in g_touch and mark a report
//        ready; the firmware acknowledges it by writing 0 to the status.
#include <Arduino.h>
#include <deque>
#include <vector>
extern std::deque<uint8_t> g_keyQueue;

struct FakeTouch {
  bool     present = false;   // a panel answers at 0x5D
  bool     ready = false;     // a report is waiting (status bit 7)
  uint8_t  fingers = 0;
  uint16_t x = 0, y = 0;      // raw panel coordinates of the first finger
};
extern FakeTouch g_touch;

struct TwoWire {
  int pending = -1;                       // keyboard byte
  uint8_t addr = 0;
  uint16_t reg = 0;
  std::vector<uint8_t> wbuf;
  std::deque<uint8_t> rx;                 // touch bytes to hand back
  bool begin(int = -1, int = -1, uint32_t = 0) { return true; }
  void setClock(uint32_t) {}
  uint8_t touchReg(uint16_t r) {
    switch (r) {
      case 0x8140: return '9';
      case 0x8141: return '1';
      case 0x8142: return '1';
      case 0x8143: return 0;
      case 0x814E: return (g_touch.ready ? 0x80 : 0) | (g_touch.fingers & 0x0F);
      case 0x814F: return 0;
      case 0x8150: return g_touch.x & 0xFF;
      case 0x8151: return g_touch.x >> 8;
      case 0x8152: return g_touch.y & 0xFF;
      case 0x8153: return g_touch.y >> 8;
      default:     return 0;
    }
  }
  uint8_t requestFrom(uint8_t a, uint8_t n) {
    // A real I2C read takes about a millisecond. Time passing here, in the
    // middle of a frame, is what exposed the screen blanking on a keypress.
    extern uint32_t g_millis; g_millis += 1;
    if (a == 0x5D) {
      if (!g_touch.present) return 0;
      rx.clear();
      for (uint8_t i = 0; i < n; i++) rx.push_back(touchReg(reg + i));
      return n;
    }
    pending = g_keyQueue.empty() ? 0 : g_keyQueue.front();
    if (!g_keyQueue.empty()) g_keyQueue.pop_front();
    return n ? 1 : 0;
  }
  int  available() { return !rx.empty() ? (int)rx.size() : pending >= 0 ? 1 : 0; }
  int  read() {
    if (!rx.empty()) { int v = rx.front(); rx.pop_front(); return v; }
    int v = pending; pending = -1; return v;
  }
  void beginTransmission(uint8_t a) { addr = a; wbuf.clear(); }
  size_t write(uint8_t b) { wbuf.push_back(b); return 1; }
  uint8_t endTransmission(bool = true) {
    if (addr == 0x5D || addr == 0x14) {
      if (!g_touch.present || addr != 0x5D) return 2;        // NACK: nobody there
      if (wbuf.size() >= 2) reg = (uint16_t)((wbuf[0] << 8) | wbuf[1]);
      if (wbuf.size() == 3 && reg == 0x814E && wbuf[2] == 0) g_touch.ready = false;   // acknowledged
    }
    return 0;
  }
};
extern TwoWire Wire;
