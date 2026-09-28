#pragma once
// ─────────────────────────────────────────────
//  LilyGo T-Deck: keyboard, trackball and the status strip
// ─────────────────────────────────────────────
//
//  Included from cypher32.ino just above setup(), so everything the game
//  defines is in scope. Nothing in here changes the rules or the radio: every
//  action a key can take is one the web portal can already take, done through
//  the same functions the portal calls. The portal is still where recon, hacks
//  and skills live; the keyboard is for the things you want without getting
//  your phone out.
//
//  CONTROLS
//    Trackball roll            previous / next page
//    Trackball press, SPACE    next page (the Wireless Paper's PRG press)
//    M                         write a message to someone in range
//    Q                         put the Wi-Fi join QR on screen for a minute
//    B                         send a beacon now
//    T                         cycle colour theme (remembered)
//    W W W                     arm factory reset, then hold the trackball 5 s
//
//  In the composer: roll to choose who, type up to 32 characters, ENTER or a
//  trackball press sends, BACKSPACE on an empty line closes it.

#define TDECK_KB_POLL_MS      25UL
#define TDECK_KB_IDLE_MS    1000UL   // poll rate once the keyboard stops answering
#define TDECK_TB_STEP          2     // trackball edges per step
#define TDECK_TB_GAP_MS      120UL   // at most one step per this, so a flick is one page
#define TDECK_STRIP_MS       500UL   // status strip refresh
#define TDECK_TOAST_MS      4000UL
#define TDECK_WIPE_TAPS        3
#define TDECK_WIPE_WINDOW_MS 3000UL
#define TDECK_MSG_MAX         32     // PktMsg carries 32 characters

// ── Persistence ─────────────────────────────
//  Its own namespace, so a factory reset of the game leaves the colour alone
//  and nothing here can collide with a key the game adds later.
void tdeckLoadPrefs() {
  preferences.begin("c32-tdeck", true);
  tdeckTheme = preferences.getUChar("theme", 0);
  preferences.end();
  if (tdeckTheme >= TDECK_THEME_COUNT) tdeckTheme = 0;
}

void tdeckSavePrefs() {
  preferences.begin("c32-tdeck", false);
  preferences.putUChar("theme", (uint8_t)tdeckTheme);
  preferences.end();
}

// ── Trackball ───────────────────────────────
//  Each direction is a hall sensor that pulses as the ball turns. The ISRs
//  only count; loop() turns counts into steps.
volatile int16_t tdeckTbX = 0, tdeckTbY = 0;
void IRAM_ATTR tdeckTbUp()    { tdeckTbY--; }
void IRAM_ATTR tdeckTbDown()  { tdeckTbY++; }
void IRAM_ATTR tdeckTbLeft()  { tdeckTbX--; }
void IRAM_ATTR tdeckTbRight() { tdeckTbX++; }

bool     tdeckKbPresent = true;
uint32_t tdeckKbLastPoll = 0;

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

// The keyboard's own ESP32-C3 hands over one byte per keypress, 0 when idle.
char tdeckReadKey() {
  uint32_t gap = tdeckKbPresent ? TDECK_KB_POLL_MS : TDECK_KB_IDLE_MS;
  if ((uint32_t)(millis() - tdeckKbLastPoll) < gap) return 0;
  tdeckKbLastPoll = millis();
  if (Wire.requestFrom((uint8_t)TDECK_KB_ADDR, (uint8_t)1) != 1) {
    tdeckKbPresent = false;
    return 0;
  }
  tdeckKbPresent = true;
  return (char)Wire.read();
}

// ── Toasts ──────────────────────────────────
String   tdeckToastText;
uint32_t tdeckToastUntil = 0;
bool     tdeckStripDirty = true;

void tdeckToast(const String& s) {
  tdeckToastText  = s;
  tdeckToastUntil = millis() + TDECK_TOAST_MS;
  tdeckStripDirty = true;
  Serial.printf("[TDECK] %s\n", s.c_str());
}

// ── Pages ───────────────────────────────────
//  The same three pages PRG cycles, stepped either way. Goes through
//  pageWanted so servicePageButton() still coalesces a fast roll into one
//  repaint and the idle-return timer still applies.
static bool tdeckConfigured() { return !(myName == "" || myFaction == "NONE"); }

void tdeckPageStep(int dir) {
  if (resetArmed || !tdeckConfigured()) return;
  pageWanted  = (uint8_t)((pageWanted + PAGE_COUNT + dir) % PAGE_COUNT);
  pageDirtyAt = millis();
  revertIdleAtMs = 0;
}

// ── Composer ────────────────────────────────
bool     tdeckComposing = false;
uint32_t tdeckComposeTo = 0;
String   tdeckComposeText;

static int tdeckNodeIndex(uint32_t id) {
  for (int i = 0; i < knownCount; i++) if (knownNodes[i].chip_id == id) return i;
  return -1;
}

// Keep the chosen recipient valid as nodes come and go.
static void tdeckComposeFixTarget() {
  if (knownCount == 0) { tdeckComposeTo = 0; return; }
  if (tdeckNodeIndex(tdeckComposeTo) < 0) tdeckComposeTo = knownNodes[0].chip_id;
}

static void tdeckComposeStep(int dir) {
  if (knownCount == 0) return;
  int i = tdeckNodeIndex(tdeckComposeTo);
  if (i < 0) i = 0; else i = (i + knownCount + dir) % knownCount;
  tdeckComposeTo = knownNodes[i].chip_id;
  tdeckStripDirty = true;
}

void tdeckComposeOpen() {
  if (!loraReady) { tdeckToast("Radio offline"); return; }
  tdeckComposing = true;
  tdeckComposeText = "";
  tdeckComposeFixTarget();
  tdeckStripDirty = true;
}

void tdeckComposeClose() {
  tdeckComposing = false;
  tdeckComposeText = "";
  tdeckStripDirty = true;
}

// What the portal's "msg" action does, from the keyboard.
void tdeckComposeSend() {
  tdeckComposeFixTarget();
  KnownNode* n = findNode(tdeckComposeTo);
  if (tdeckComposeText.length() == 0) { tdeckToast("Type something first"); return; }
  if (!n)                             { tdeckToast("Nobody in range to send to"); return; }
  if (!loraReady)                     { tdeckToast("Radio offline"); return; }
  if (loraActionPending())            { tdeckToast("Radio busy - try again"); return; }
  uint32_t target = tdeckComposeTo;
  String   txt    = tdeckComposeText;
  strncpy(n->msg_sent, txt.c_str(), 32); n->msg_sent[32] = '\0';
  lastSentTo = target; lastSentText = txt; lastSentAt = millis();
  logEvent(EV_MSG_OUT, target, 0);
  loraSendMsg(target, txt.c_str());
  tdeckComposeClose();
  tdeckToast("Sent to " + nodeDisplayName(target));
  if (pageShown == PAGE_LASTMSG) paintCurrentPage();
}

// Called from servicePageButton() for each short trackball press.
bool tdeckTakesClick() {
  if (!tdeckComposing) return false;
  tdeckComposeSend();
  return true;
}

// ── Factory reset from the keyboard ─────────
//  The T-Deck has no RST button, so the Wireless Paper's "double tap RST"
//  cannot be done on it (a quick double power-cycle on battery still works).
//  Three W presses in three seconds do the same job: they only ARM the wipe,
//  and the trackball still has to be held for five seconds to confirm it —
//  serviceFactoryResetButton() handles that part exactly as before.
static uint32_t tdeckWipeTapAt[TDECK_WIPE_TAPS];
static uint8_t  tdeckWipeTapN = 0;

static void tdeckWipeTap() {
  uint32_t now = millis();
  tdeckWipeTapAt[tdeckWipeTapN++ % TDECK_WIPE_TAPS] = now;
  if (tdeckWipeTapN < TDECK_WIPE_TAPS) return;
  uint32_t oldest = tdeckWipeTapAt[tdeckWipeTapN % TDECK_WIPE_TAPS];
  if ((uint32_t)(now - oldest) > TDECK_WIPE_WINDOW_MS) return;
  tdeckWipeTapN = 0;
  resetArmed   = true;
  resetArmedAt = now;
  Serial.println("[FACTORY RESET] armed from the keyboard");
  paintCurrentPage();
  tdeckToast("Wipe armed: hold trackball 5s");
}

// ── Key dispatch ────────────────────────────
static void tdeckHandleKey(char k) {
  if (tdeckComposing) {
    if (k == '\r' || k == '\n') { tdeckComposeSend(); return; }
    if (k == 0x08 || k == 0x7F) {
      if (tdeckComposeText.length() == 0) tdeckComposeClose();
      else tdeckComposeText.remove(tdeckComposeText.length() - 1);
      tdeckStripDirty = true;
      return;
    }
    if (k >= 32 && k <= 126 && tdeckComposeText.length() < TDECK_MSG_MAX) {
      tdeckComposeText += k;
      tdeckStripDirty = true;
    }
    return;
  }

  char c = (k >= 'A' && k <= 'Z') ? (char)(k - 'A' + 'a') : k;
  if (c != 'w') tdeckWipeTapN = 0;
  switch (c) {
    case 't':
      tdeckSetTheme(tdeckTheme + 1);
      tdeckSavePrefs();
      if (tft) tft->fillScreen(tdeckPaper);
      display.update();                      // repaint the canvas in the new colours
      tdeckStripDirty = true;
      tdeckToast(String("Theme: ") + TDECK_THEMES[tdeckTheme].name);
      return;
    case 'w':
      tdeckWipeTap();
      return;
  }
  if (!tdeckConfigured()) {                  // the join QR stays up until set up
    tdeckToast("Set up first: join the Wi-Fi above");
    return;
  }
  switch (c) {
    case ' ': case '\r': case 'n': tdeckPageStep(+1); break;
    case 'p':                      tdeckPageStep(-1); break;
    case 'm':                      tdeckComposeOpen(); break;
    case 'q':
      displayQr(WiFi.softAPSSID(), "Open network.", "Then 192.168.4.1");
      revertIdleAtMs = millis() + 60000;
      tdeckToast("QR up for 60s - any page key closes it");
      break;
    case 'b':
      if (!loraReady) { tdeckToast("Radio offline"); break; }
      loraSendBeacon();
      tdeckToast("Beacon sent");
      break;
    default: break;
  }
}

// ── Status strip ────────────────────────────
//  Five text rows in the 84 px under the game screen. Each row is only
//  repainted when its text changes, so the strip never flickers.
#define TDECK_ROWS 5
struct TDeckRow { int y; uint8_t size; uint8_t tone; };   // tone: 0 dim, 1 mid, 2 ink
static const TDeckRow TDECK_ROW_LAYOUT[TDECK_ROWS] = {
  { 161, 1, 1 }, { 172, 1, 1 }, { 186, 2, 2 }, { 205, 2, 2 }, { 228, 1, 0 },
};
static String   tdeckRowShown[TDECK_ROWS];
static uint8_t  tdeckRowTone[TDECK_ROWS];
static int      tdeckRowTheme = -1;
static uint32_t tdeckStripAt  = 0;

static uint16_t tdeckTone(uint8_t tone) {
  return tone == 2 ? tdeckInk : tone == 1 ? tdeckMid : tdeckDim;
}

static void tdeckBuildRows(String rows[TDECK_ROWS], uint8_t tones[TDECK_ROWS]) {
  for (int i = 0; i < TDECK_ROWS; i++) { rows[i] = ""; tones[i] = TDECK_ROW_LAYOUT[i].tone; }
  bool toast = tdeckToastUntil && (int32_t)(tdeckToastUntil - millis()) > 0;

  if (tdeckComposing) {
    tdeckComposeFixTarget();
    if (knownCount == 0) {
      rows[0] = "MSG TO  (nobody in range yet)";
    } else {
      int i = tdeckNodeIndex(tdeckComposeTo);
      rows[0] = "MSG TO  " + nodeDisplayName(tdeckComposeTo) +
                "   " + String(i + 1) + "/" + String(knownCount);
    }
    rows[1] = "ball=who  ENTER=send  BKSP=erase/close";
    String t = "> " + tdeckComposeText + "_";
    const int PER = TDECK_TFT_W / 12;          // size-2 characters per row
    rows[2] = t.substring(0, min((int)t.length(), PER));
    rows[3] = (int)t.length() > PER ? t.substring(PER) : String("");
    rows[4] = toast ? tdeckToastText
                    : String(tdeckComposeText.length()) + "/" + String(TDECK_MSG_MAX);
    if (toast) tones[4] = 2;
    return;
  }

  String radio = loraReady ? String("LORA ") + PROFILE_NAME + " " + String(LORA_FREQ, 3)
                           : String("LORA ") + loraStatus;
  rows[0] = radio + "   " + String(knownCount) + " in range";
  rows[1] = "WIFI " + WiFi.softAPSSID() + "  > 192.168.4.1";
  if (toast) { rows[2] = tdeckToastText; tones[2] = 2; }
  if (!tdeckConfigured())
    rows[4] = "Scan the QR to set up    T:theme";
  else if (resetArmed)
    rows[4] = "HOLD TRACKBALL 5s TO WIPE - or wait";
  else
    rows[4] = "ball:page M:msg Q:QR B:beacon T:theme";
}

// Toasts are one line of plain text; drop to size 1 when they would not fit.
static void tdeckDrawRow(int i, const String& s, uint8_t tone) {
  const TDeckRow& r = TDECK_ROW_LAYOUT[i];
  uint8_t size = r.size;
  if (size == 2 && (int)s.length() * 12 > TDECK_TFT_W) size = 1;
  int h = (i == 2 || i == 3) ? 18 : 10;
  tft->fillRect(0, r.y - 1, TDECK_TFT_W, h, tdeckPaper);
  tft->setTextSize(size);
  tft->setTextColor(tdeckTone(tone), tdeckPaper);
  tft->setCursor(4, r.y + (size == 1 && r.size == 2 ? 4 : 0));
  tft->print(s);
}

void tdeckDrawStrip(bool force) {
  if (!tft) return;
  if (force || tdeckRowTheme != tdeckTheme) {
    tft->fillRect(0, TDECK_STRIP_Y, TDECK_TFT_W, TDECK_STRIP_H, tdeckPaper);
    tft->drawFastHLine(0, TDECK_STRIP_Y + 1, TDECK_TFT_W, tdeckDim);
    for (int i = 0; i < TDECK_ROWS; i++) tdeckRowShown[i] = "\x01";   // never matches
    tdeckRowTheme = tdeckTheme;
  }
  String  rows[TDECK_ROWS];
  uint8_t tones[TDECK_ROWS];
  tdeckBuildRows(rows, tones);
  for (int i = 0; i < TDECK_ROWS; i++) {
    if (rows[i] == tdeckRowShown[i] && tones[i] == tdeckRowTone[i]) continue;
    tdeckDrawRow(i, rows[i], tones[i]);
    tdeckRowShown[i] = rows[i];
    tdeckRowTone[i]  = tones[i];
  }
}

// ── Once per loop() ─────────────────────────
void tdeckTick() {
  // Keys. Drain a few per pass so a fast typist is not rate-limited by the
  // poll interval, but never spin here.
  for (int i = 0; i < 4; i++) {
    char k = tdeckReadKey();
    if (!k) break;
    tdeckHandleKey(k);
    tdeckKbLastPoll = 0;                       // there may be more waiting
  }

  // Trackball. Vertical and horizontal both step, whichever moved further.
  static uint32_t lastStep = 0;
  int16_t dx = tdeckTbX, dy = tdeckTbY;
  int16_t mag = max(abs(dx), abs(dy));
  if (mag >= TDECK_TB_STEP) {
    noInterrupts(); tdeckTbX = 0; tdeckTbY = 0; interrupts();
    if ((uint32_t)(millis() - lastStep) >= TDECK_TB_GAP_MS) {
      lastStep = millis();
      int dir = (abs(dx) >= abs(dy)) ? (dx > 0 ? 1 : -1) : (dy > 0 ? 1 : -1);
      if (tdeckComposing) tdeckComposeStep(dir);
      else                tdeckPageStep(dir);
    }
  }

  // Toast expiry and periodic refresh of the live numbers.
  if (tdeckToastUntil && (int32_t)(millis() - tdeckToastUntil) >= 0) {
    tdeckToastUntil = 0;
    tdeckStripDirty = true;
  }
  if (tdeckStripDirty || (uint32_t)(millis() - tdeckStripAt) >= TDECK_STRIP_MS) {
    tdeckStripDirty = false;
    tdeckStripAt = millis();
    tdeckDrawStrip(false);
  }
}
