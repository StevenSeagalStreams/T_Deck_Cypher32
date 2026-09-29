#pragma once
// ─────────────────────────────────────────────
//  LilyGo T-Deck hardware layer
// ─────────────────────────────────────────────
//
//  Only compiled for the T-Deck (CYPHER32_TDECK, the default for any ESP32
//  build of this repository). Pins, power, the screen, the keyboard, the
//  trackball and the backlight. The game's own screens live in tdeck_app.h.
//
//  PIN MAP (LilyGo T-Deck; LilyGo's utilities.h and Meshtastic's variant.h)
//    Peripheral power enable  GPIO10   — must be HIGH or nothing else works
//    Shared SPI bus           SCK 40, MISO 38, MOSI 41
//      TFT  ST7789 320x240    CS 12, DC 11, backlight 42 (AW9364, 16 levels)
//      SX1262                 CS 9, BUSY 13, RST 17, DIO1 45  (packets.h)
//      SD card                CS 39    (unused, but must be held high)
//    I2C                      SDA 18, SCL 8
//      Keyboard (ESP32-C3)    0x55, one byte per keypress; 0x01,duty = backlight
//    Trackball                UP 3, DOWN 15, LEFT 1, RIGHT 2, CLICK 0
//    Speaker (MAX98357A I2S)  BCK 7, WS 5, DOUT 6   (tdeck_sound.h)
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

// Adafruit's ST7789 rotation 1 puts the image upside down on the T-Deck (its
// MADCTL for 1 differs from TFT_eSPI's, which LilyGo's examples use); 3 is the
// right way up with the keyboard below the screen. Confirmed on hardware.
#ifndef TDECK_ROTATION
#define TDECK_ROTATION    3
#endif

#define TDECK_W         320
#define TDECK_H         240

// The e-ink drawing code is still compiled (the Heltec build shares it), and
// its colour words mean ink and paper. Nothing on the T-Deck shows it.
#define BLACK 1
#define WHITE 0

// The T-Deck has 8 MB of octal PSRAM, and the game draws into a 150 KB
// off-screen canvas that lives there. Without it the device would boot to a
// black screen or crash, so refuse to build rather than fail on the desk.
// In the Arduino IDE: Tools > PSRAM > "OPI PSRAM".
#if defined(ARDUINO_ARCH_ESP32) && !defined(TDECK_ALLOW_NO_PSRAM) && \
    (!defined(BOARD_HAS_PSRAM) || !defined(CONFIG_SPIRAM_MODE_OCT))
  #error "Cypher32 T-Deck needs PSRAM: in the Arduino IDE set Tools > PSRAM > OPI PSRAM"
#endif

// ── Hooks the sketch calls into the T-Deck app (tdeck_app.h) ──
// The sketch's e-ink screens are where every game event already surfaces —
// a hack verdict, a message, a new node — so on the T-Deck each of those
// functions hands its event to the app instead of drawing.
void tdeckRedraw();
void tdeckEvtHack(bool won, const String& tid, int xp, const String& note);
void tdeckEvtHackTimeout();
void tdeckEvtDefense(uint32_t who, bool breached);
void tdeckEvtMessage(const String& fromId, const String& msg);
void tdeckEvtLevelUp();
void tdeckEvtNewNode(uint32_t id);
void tdeckEvtScouted(uint32_t who);
void tdeckEvtArmed();
void tdeckEvtWiping();
void tdeckEvtQr();
bool tdeckPortalEnabled();

// The e-ink code's display object. Kept so that code compiles unchanged; it
// is a small canvas nobody looks at, because every display* function routes
// to the app before it draws.
class TDeckPanel : public GFXcanvas1 {
public:
  TDeckPanel() : GFXcanvas1(250, 122) {}
  void clearMemory() { fillScreen(WHITE); }
  void landscape()   {}
  void update()      {}
};

Adafruit_ST7789* tft = nullptr;

// ── Backlight ───────────────────────────────
//  The AW9364 counts pulses: HIGH = level 16 (brightest), each LOW→HIGH blip
//  steps one level down (wrapping), LOW for 3 ms = off. From LilyGo's UnitTest.
uint8_t tdeckBlLevel = 0;
void tdeckBacklight(uint8_t v) {                       // 0 (off) .. 16
  const uint8_t steps = 16;
  if (v > steps) v = steps;
  if (v == tdeckBlLevel) return;
  if (!v) { digitalWrite(TDECK_TFT_BL, LOW); delay(3); tdeckBlLevel = 0; return; }
  if (!tdeckBlLevel) { digitalWrite(TDECK_TFT_BL, HIGH); tdeckBlLevel = steps; delayMicroseconds(30); }
  int num = (steps + (steps - v) - (steps - tdeckBlLevel)) % steps;
  // The AW9364 needs each low and high to last at least ~0.5 us; back-to-back
  // digitalWrite()s can be shorter than that, and a missed pulse leaves the
  // level wrong until the next off.
  for (int i = 0; i < num; i++) {
    digitalWrite(TDECK_TFT_BL, LOW);  delayMicroseconds(1);
    digitalWrite(TDECK_TFT_BL, HIGH); delayMicroseconds(1);
  }
  tdeckBlLevel = v;
}

// ── Power and bus bring-up ──────────────────
//  GPIO10 powers the TFT, radio, keyboard and SD slot; every chip select on
//  the shared bus has to be high before anything talks on it, or the SD card
//  answers commands meant for the radio. The bus is begun here with the
//  T-Deck's pins so the radio's own loraSPI.begin() later finds it running
//  and leaves it alone — which is what lets the two drivers share it.
void tdeckPowerOn() {
  pinMode(TDECK_POWERON, OUTPUT);
  digitalWrite(TDECK_POWERON, HIGH);
  const int cs[] = { TDECK_SD_CS, LORA_NSS, TDECK_TFT_CS };
  for (int p : cs) { pinMode(p, OUTPUT); digitalWrite(p, HIGH); }
  pinMode(TDECK_TFT_BL, OUTPUT);
  digitalWrite(TDECK_TFT_BL, LOW);                    // dark until the first frame
  loraSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI);
}

void tdeckDisplayBegin() {
  if (!tft) tft = new Adafruit_ST7789(&loraSPI, TDECK_TFT_CS, TDECK_TFT_DC, -1);
  tft->init(240, 320);
  tft->setSPISpeed(40000000);
  tft->setRotation(TDECK_ROTATION);
  tft->fillScreen(0x0000);
}

// Screen power: the backlight is most of the draw, the panel's own sleep
// the rest. Waking takes ~120 ms (the controller's SLPOUT delay).
//
// The ST7789 needs 120 ms between SLPIN and SLPOUT (either way round) and
// 5 ms after SLPOUT before the next command; Adafruit's enableSleep() does
// not wait, so it is done here. The backlight comes back last, via the
// caller, after a fresh frame is on the glass.
bool     tdeckScreenOn = true;
uint32_t tdeckScreenAt = 0;                            // last SLPIN/SLPOUT
void tdeckScreenPower(bool on) {
  if (!tft || on == tdeckScreenOn) return;
  uint32_t since = millis() - tdeckScreenAt;
  if (tdeckScreenAt && since < 120) delay(120 - since);
  if (on) { tft->enableSleep(false); delay(5); tft->enableDisplay(true); }
  else    { tdeckBacklight(0); tft->enableDisplay(false); tft->enableSleep(true); }
  tdeckScreenAt = millis();
  tdeckScreenOn = on;
}

// ── Keyboard ────────────────────────────────
//  The keyboard's own ESP32-C3 hands over one byte per keypress, 0 when idle
//  (Enter 0x0D, Backspace 0x08; Sym + W E R / S D F / Z X C gives 1-9). It
//  keeps only the last key, so it is polled often.
bool     tdeckKbPresent = true;
uint32_t tdeckKbLastPoll = 0;
uint8_t  tdeckKbMisses = 0;

bool tdeckKbFast = false;              // poll every 4 ms (the breach game)
char tdeckReadKey() {
  uint32_t gap = !tdeckKbPresent ? 1000 : tdeckKbFast ? 4 : 15;
  if ((uint32_t)(millis() - tdeckKbLastPoll) < gap) return 0;
  tdeckKbLastPoll = millis();
  // One NACK is a hiccup; three in a row is a keyboard that is not there,
  // and it is then polled once a second instead.
  if (Wire.requestFrom((uint8_t)TDECK_KB_ADDR, (uint8_t)1) != 1) {
    if (tdeckKbMisses < 3 && ++tdeckKbMisses >= 3) tdeckKbPresent = false;
    return 0;
  }
  tdeckKbMisses = 0;
  tdeckKbPresent = true;
  int c = Wire.read();
  return c < 0 ? 0 : (char)c;
}

// Keyboard backlight: supported by the C3 firmware LilyGo ships since late
// 2024. Older keyboards ignore it (Alt+B still toggles it locally).
void tdeckKbBacklight(uint8_t duty) {
  Wire.beginTransmission(TDECK_KB_ADDR);
  Wire.write(0x01); Wire.write(duty);
  Wire.endTransmission();
}

// ── Trackball ───────────────────────────────
//  Each direction is a hall sensor that pulses as the ball turns. The ISRs
//  only count; the app turns counts into steps.
volatile int16_t tdeckTbX = 0, tdeckTbY = 0;
void IRAM_ATTR tdeckTbUp()    { tdeckTbY = tdeckTbY - 1; }
void IRAM_ATTR tdeckTbDown()  { tdeckTbY = tdeckTbY + 1; }
void IRAM_ATTR tdeckTbLeft()  { tdeckTbX = tdeckTbX - 1; }
void IRAM_ATTR tdeckTbRight() { tdeckTbX = tdeckTbX + 1; }

void tdeckInputBegin() {
  Wire.begin(TDECK_I2C_SDA, TDECK_I2C_SCL);
  const int pins[] = { TDECK_TB_UP, TDECK_TB_DOWN, TDECK_TB_LEFT, TDECK_TB_RIGHT };
  for (int p : pins) pinMode(p, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(TDECK_TB_UP),    tdeckTbUp,    FALLING);
  attachInterrupt(digitalPinToInterrupt(TDECK_TB_DOWN),  tdeckTbDown,  FALLING);
  attachInterrupt(digitalPinToInterrupt(TDECK_TB_LEFT),  tdeckTbLeft,  FALLING);
  attachInterrupt(digitalPinToInterrupt(TDECK_TB_RIGHT), tdeckTbRight, FALLING);
  Wire.requestFrom((uint8_t)TDECK_KB_ADDR, (uint8_t)1);
  tdeckKbPresent = Wire.available() > 0;
  while (Wire.available()) Wire.read();
  Serial.printf("[TDECK] keyboard %s\n", tdeckKbPresent ? "found" : "not answering");
}
