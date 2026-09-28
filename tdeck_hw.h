#pragma once
// ─────────────────────────────────────────────
//  LilyGo T-Deck hardware layer
// ─────────────────────────────────────────────
//
//  Only compiled when CYPHER32_TDECK is defined (platformio.ini sets it).
//
//  The game was written for a 250x122 e-ink panel and every screen in
//  cypher32.ino is laid out in those pixels. Rather than fork forty drawing
//  functions, this file gives the sketch a drop-in replacement for the Heltec
//  display object: the sketch draws into a 250x122 one-bit canvas exactly as it
//  always has, and update() scales that canvas 1.28x onto the top 320x156 of
//  the T-Deck's ST7789, anti-aliased and tinted by the current theme. The
//  84 px strip left underneath belongs to tdeck_ui.h — radio status, key hints
//  and the on-device message composer.
//
//  The radio code is untouched apart from its pin numbers, so a T-Deck and a
//  Wireless Paper running the same range profile hear each other exactly as
//  two Wireless Papers do.
//
//  PIN MAP (LilyGo T-Deck, from LilyGo's utilities.h)
//    Peripheral power enable  GPIO10   — must be HIGH or nothing else works
//    Shared SPI bus           SCK 40, MISO 38, MOSI 41
//      TFT  ST7789 320x240    CS 12, DC 11, backlight 42
//      SX1262                 CS 9, BUSY 13, RST 17, DIO1 45  (packets.h)
//      SD card                CS 39    (unused, but must be held high)
//    I2C                      SDA 18, SCL 8
//      Keyboard (ESP32-C3)    0x55, one byte per keypress
//    Trackball                UP 3, DOWN 15, LEFT 1, RIGHT 2, CLICK 0
//    Battery sense            GPIO4 through a 1:2 divider
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

#define TDECK_POWERON    10
#define TDECK_I2C_SDA    18
#define TDECK_I2C_SCL     8
#define TDECK_KB_ADDR  0x55
#define TDECK_TB_UP       3
#define TDECK_TB_DOWN    15
#define TDECK_TB_LEFT     1
#define TDECK_TB_RIGHT    2
#define TDECK_TB_CLICK    0
#define TDECK_TFT_CS     12
#define TDECK_TFT_DC     11
#define TDECK_TFT_BL     42
#define TDECK_SD_CS      39
#define TDECK_BAT_ADC     4

#define TDECK_TFT_W     320
#define TDECK_TFT_H     240
#define TDECK_SRC_W     250     // the e-ink layout the sketch draws in
#define TDECK_SRC_H     122
#define TDECK_GAME_H    156     // 122 * 320/250, rounded down
#define TDECK_STRIP_Y   TDECK_GAME_H
#define TDECK_STRIP_H   (TDECK_TFT_H - TDECK_GAME_H)

// The sketch's colour words mean "ink" and "paper", not RGB. On the canvas a
// set bit is ink; the theme decides what colour ink is.
#define BLACK 1
#define WHITE 0

// Defined in tdeck_ui.h, which is included further down the sketch; the page
// button handler needs to ask it whether a press belongs to the composer.
bool tdeckTakesClick();

static inline constexpr uint16_t tdeckRgb(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

struct TDeckTheme {
  const char* name;
  uint8_t paper[3];
  uint8_t ink[3];
};

// Phosphor is the default: the character reads as a hooded figure on a dark
// terminal, which is the whole mood of the game. Paper is the original e-ink
// look for anyone who wants their T-Deck to match their friends' Heltecs.
static const TDeckTheme TDECK_THEMES[] = {
  { "PHOSPHOR", {   4,  10,   6 }, {  60, 255, 120 } },
  { "AMBER",    {  10,   6,   0 }, { 255, 176,   0 } },
  { "PAPER",    { 232, 230, 220 }, {  16,  16,  16 } },
  { "ICE",      {   2,   6,  14 }, { 110, 200, 255 } },
};
#define TDECK_THEME_COUNT ((int)(sizeof(TDECK_THEMES) / sizeof(TDECK_THEMES[0])))

// One TFT object, sharing the radio's SPI bus. Constructed in tdeckPowerOn(),
// after the bus exists.
Adafruit_ST7789* tft = nullptr;

// Colour lookup: 33 ink levels from paper (0) to full ink (32), rebuilt when
// the theme changes. The strip colours are picked from the same ramp so the
// whole screen stays in one palette.
uint16_t tdeckLut[33];
uint16_t tdeckPaperLut[33];   // fixed dark-on-light ramp for QR frames
uint16_t tdeckPaper = 0, tdeckInk = 0xFFFF, tdeckDim = 0x7BEF, tdeckMid = 0xBDF7;
int      tdeckTheme = 0;

static void tdeckBuildRamp(const TDeckTheme& th, uint16_t* lut) {
  for (int i = 0; i <= 32; i++) {
    uint8_t c[3];
    for (int k = 0; k < 3; k++)
      c[k] = (uint8_t)(th.paper[k] + ((int)th.ink[k] - th.paper[k]) * i / 32);
    lut[i] = tdeckRgb(c[0], c[1], c[2]);
  }
}

void tdeckSetTheme(int t) {
  tdeckTheme = ((t % TDECK_THEME_COUNT) + TDECK_THEME_COUNT) % TDECK_THEME_COUNT;
  tdeckBuildRamp(TDECK_THEMES[tdeckTheme], tdeckLut);
  tdeckBuildRamp(TDECK_THEMES[2], tdeckPaperLut);             // PAPER
  tdeckPaper = tdeckLut[0];
  tdeckInk   = tdeckLut[32];
  tdeckDim   = tdeckLut[12];
  tdeckMid   = tdeckLut[22];
}

// ── Scaling tables ──────────────────────────
//
//  Each destination pixel covers 250/320 of a source pixel on each axis, so it
//  overlaps at most two source pixels per axis. s0/s1 are those two, w0 is how
//  much of the destination pixel s0 covers, out of 64.
struct TDeckTap { uint8_t s0, s1, w0; };
static TDeckTap tdeckTapX[TDECK_TFT_W];
static TDeckTap tdeckTapY[TDECK_GAME_H];

static void tdeckBuildTaps(TDeckTap* t, int S, int D) {
  for (int d = 0; d < D; d++) {
    int a = d * S, b = (d + 1) * S;
    int s0 = a / D, bnd = (s0 + 1) * D;
    int w0 = (b <= bnd) ? 64 : ((bnd - a) * 64 + S / 2) / S;
    t[d].s0 = (uint8_t)s0;
    t[d].s1 = (uint8_t)((s0 + 1 < S) ? s0 + 1 : S - 1);
    t[d].w0 = (uint8_t)w0;
  }
}

// ── The display object the sketch draws into ──
//
//  The same ten calls the Heltec driver answered: clearMemory, landscape,
//  update, and the Adafruit-GFX drawing surface (which GFXcanvas1 already is).
class TDeckPanel : public GFXcanvas1 {
public:
  TDeckPanel() : GFXcanvas1(TDECK_SRC_W, TDECK_SRC_H) {}
  void clearMemory() { fillScreen(WHITE); paperFrame = false; }
  void landscape()   {}               // the canvas is already landscape
  void update();                      // push the canvas to the glass
  uint32_t pushes = 0;
  // Set by displayQr(): phone cameras will not read a light-on-dark QR, so a
  // frame carrying one is shown dark-on-light whatever the theme. Lasts until
  // the next clearMemory(), so a theme change while it is up keeps it.
  bool paperFrame = false;
};

void TDeckPanel::update() {
  if (!tft) return;
  pushes++;
  const uint8_t* buf = getBuffer();
  const int stride = (TDECK_SRC_W + 7) / 8;
  static uint16_t line[TDECK_TFT_W];
  const uint16_t* lut = paperFrame ? tdeckPaperLut : tdeckLut;

  tft->startWrite();
  tft->setAddrWindow(0, 0, TDECK_TFT_W, TDECK_GAME_H);
  for (int dy = 0; dy < TDECK_GAME_H; dy++) {
    const TDeckTap& ty = tdeckTapY[dy];
    const uint8_t* r0 = buf + ty.s0 * stride;
    const uint8_t* r1 = buf + ty.s1 * stride;
    int wy0 = ty.w0, wy1 = 64 - ty.w0;
    for (int dx = 0; dx < TDECK_TFT_W; dx++) {
      const TDeckTap& tx = tdeckTapX[dx];
      int wx0 = tx.w0, wx1 = 64 - tx.w0;
      uint8_t m0 = 0x80 >> (tx.s0 & 7), m1 = 0x80 >> (tx.s1 & 7);
      int b0 = tx.s0 >> 3, b1 = tx.s1 >> 3;
      int v = 0;                                      // 0..4096
      if (r0[b0] & m0) v += wx0 * wy0;
      if (r0[b1] & m1) v += wx1 * wy0;
      if (r1[b0] & m0) v += wx0 * wy1;
      if (r1[b1] & m1) v += wx1 * wy1;
      // >>7 gives 0..32; the 3/2 gain pulls thin strokes back up to full ink
      // so 6x8 text stays crisp instead of turning grey at 1.28x.
      v = ((v >> 7) * 3) >> 1;
      if (v > 32) v = 32;
      line[dx] = lut[v];
    }
    tft->writePixels(line, TDECK_TFT_W);
  }
  tft->endWrite();
}

// ── Power and bus bring-up ──────────────────
//
//  Order matters. GPIO10 powers the TFT, radio, keyboard and SD slot; every
//  chip select on the shared bus has to be high before anything talks on it,
//  or the SD card answers commands meant for the radio. The bus is begun here
//  with the T-Deck's pins so that the radio's own loraSPI.begin() later finds
//  it already running and leaves it alone — SPIClass::begin() returns early
//  once initialised, which is what lets the two drivers share it.
void tdeckPowerOn() {
  pinMode(TDECK_POWERON, OUTPUT);
  digitalWrite(TDECK_POWERON, HIGH);
  const int cs[] = { TDECK_SD_CS, LORA_NSS, TDECK_TFT_CS };
  for (int p : cs) { pinMode(p, OUTPUT); digitalWrite(p, HIGH); }
  pinMode(TDECK_TFT_BL, OUTPUT);
  digitalWrite(TDECK_TFT_BL, LOW);          // dark until the first frame
  loraSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI);
}

void tdeckDisplayBegin() {
  tdeckBuildTaps(tdeckTapX, TDECK_SRC_W, TDECK_TFT_W);
  tdeckBuildTaps(tdeckTapY, TDECK_SRC_H, TDECK_GAME_H);
  tdeckSetTheme(tdeckTheme);
  if (!tft) tft = new Adafruit_ST7789(&loraSPI, TDECK_TFT_CS, TDECK_TFT_DC, -1);
  tft->init(240, 320);
  tft->setSPISpeed(40000000);
  tft->setRotation(1);                      // landscape, keyboard at the bottom
  tft->fillScreen(tdeckPaper);
  digitalWrite(TDECK_TFT_BL, HIGH);
}
