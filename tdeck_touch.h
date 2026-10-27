#pragma once
// ─────────────────────────────────────────────
//  T-Deck touch panel: GT911 capacitive controller
// ─────────────────────────────────────────────
//
//  On the same I2C bus as the keyboard (SDA 18 / SCL 8), at 0x5D — or 0x14,
//  depending on how the controller latched its address at power-up, so both
//  are tried. Its interrupt line is GPIO16. LilyGo's own firmware uses the
//  same part ("GT911" in its factory image).
//
//  Talked to directly, register by register, rather than through a library:
//  three registers are all a game needs, and it keeps the Arduino IDE install
//  to the libraries it already has.
//
//    0x8140..43  product id, "911"
//    0x814E      status: bit 7 = a new report is ready, bits 0..3 = fingers
//    0x814F..56  first finger: id, x lo/hi, y lo/hi, size lo/hi
//  A report is acknowledged by writing 0 back to 0x814E.
//
//  The controller reports in its own portrait coordinates. Turning those
//  into screen pixels is a calibration (tdeck_app.h), with a default that
//  matches LilyGo's mapping for this panel.

#define TDECK_TOUCH_INT    16
#define GT911_REG_PID      0x8140
#define GT911_REG_STATUS   0x814E
#define GT911_REG_POINT1   0x814F

uint8_t tdeckTouchAddr = 0;                 // 0: no panel found
// The moment the controller last signalled a report: a touch's start time
// for the breach game, more exact than when the loop got round to polling.
volatile uint32_t tdeckTouchIrqMs = 0;
void IRAM_ATTR tdeckTouchIsr() { tdeckTouchIrqMs = millis(); }

static bool gt911Read(uint16_t reg, uint8_t* buf, uint8_t n) {
  Wire.beginTransmission(tdeckTouchAddr);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(tdeckTouchAddr, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = (uint8_t)Wire.read();
  return true;
}
static void gt911Write8(uint16_t reg, uint8_t v) {
  Wire.beginTransmission(tdeckTouchAddr);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  Wire.write(v);
  Wire.endTransmission();
}

// Find the panel. Call after Wire.begin().
bool tdeckTouchBegin() {
  pinMode(TDECK_TOUCH_INT, INPUT);
  const uint8_t addrs[] = { 0x5D, 0x14 };
  for (uint8_t a : addrs) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { tdeckTouchAddr = a; break; }
  }
  if (!tdeckTouchAddr) return false;
  uint8_t pid[4] = { 0 };
  if (!gt911Read(GT911_REG_PID, pid, 4) || pid[0] != '9') {   // something else answered
    tdeckTouchAddr = 0;
    return false;
  }
  gt911Write8(GT911_REG_STATUS, 0);           // drop anything stale
  attachInterrupt(digitalPinToInterrupt(TDECK_TOUCH_INT), tdeckTouchIsr, CHANGE);
  return true;
}

// Poll the controller. Returns -1 when there is nothing new (the finger is
// where it was), 0 when it reports no finger, 1 with the first finger's raw
// position in rx/ry.
int tdeckTouchPoll(int16_t& rx, int16_t& ry) {
  if (!tdeckTouchAddr) return -1;
  uint8_t st = 0;
  if (!gt911Read(GT911_REG_STATUS, &st, 1)) return -1;
  if (!(st & 0x80)) return -1;
  int fingers = st & 0x0F;
  int res = 0;
  if (fingers > 0 && fingers <= 5) {
    uint8_t p[7];
    if (gt911Read(GT911_REG_POINT1, p, 7)) {
      rx = (int16_t)(p[1] | (p[2] << 8));
      ry = (int16_t)(p[3] | (p[4] << 8));
      res = 1;
    } else res = -1;
  }
  gt911Write8(GT911_REG_STATUS, 0);
  return res;
}
