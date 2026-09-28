#pragma once
// Host stub for the T-Deck's TFT: a real Adafruit_GFX (compiled from the
// library) drawing into a 320x240 RGB565 framebuffer that tests dump to PNG.
#include <Adafruit_GFX.h>
#include <SPI.h>
class Adafruit_ST7789 : public Adafruit_GFX {
public:
  static const int W = 320, H = 240;
  uint16_t fb[H][W];
  int wx = 0, wy = 0, ww = 0, wh = 0, wpos = 0;
  uint32_t pixelsPushed = 0;
  Adafruit_ST7789(SPIClass*, int8_t, int8_t, int8_t) : Adafruit_GFX(240, 320) { memset(fb, 0, sizeof fb); }
  void init(uint16_t, uint16_t, uint8_t = 0) {}
  void setSPISpeed(uint32_t) {}
  void invertDisplay(bool) {}
  void enableDisplay(bool) {}
  void enableSleep(bool) {}
  void drawPixel(int16_t x, int16_t y, uint16_t c) override {
    if (x < 0 || y < 0 || x >= width() || y >= height()) return;
    fb[y][x] = c; pixelsPushed++;
  }
  void startWrite() override {}
  void endWrite() override {}
  void setAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) { wx = x; wy = y; ww = w; wh = h; wpos = 0; }
  void writePixels(uint16_t* c, uint32_t n, bool = true, bool = false) {
    while (n--) { int x = wx + wpos % ww, y = wy + wpos / ww; drawPixel(x, y, *c++); wpos++; }
  }
  void drawRGBBitmap(int16_t x, int16_t y, const uint16_t* b, int16_t w, int16_t h) {
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) drawPixel(x + i, y + j, b[j * w + i]);
  }
  // PPM: trivially readable by Pillow, no image library needed here.
  void writePPM(const char* path) const {
    FILE* f = fopen(path, "wb"); if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
      uint16_t c = fb[y][x];
      uint8_t p[3] = { (uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 63) << 2), (uint8_t)((c & 31) << 3) };
      fwrite(p, 1, 3, f);
    }
    fclose(f);
  }
};
#define ST77XX_BLACK 0x0000
