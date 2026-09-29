#pragma once
// ─────────────────────────────────────────────
//  Cypher32 on the T-Deck: the standalone game
// ─────────────────────────────────────────────
//
//  Everything the web portal did, on the device itself: setup, radar, recon,
//  hacking, messages, skills, the log and settings — plus the living avatar,
//  sound, notifications and power saving. Included from cypher32.ino just
//  above setup(), so the whole game is in scope.
//
//  Rules live in the sketch, not here. Every move goes through the same
//  act*() functions the portal calls (see GAME ACTIONS in cypher32.ino), so
//  the two front ends cannot disagree, and nothing here changes what goes
//  over the air.
//
//  CONTROLS (shown on screen, always)
//    Trackball ◄ ►         change tab            H R I K L O  jump to a tab
//    Trackball ▲ ▼         move the selection
//    Press / ENTER         do the highlighted thing
//    BACKSPACE             back / close
//    M                     write a message, from anywhere
//
//  Layout, 320 x 240:
//    y   0..15   status bar   who you are, XP, radio, mail, battery
//    y  16..29   tab strip
//    y  30..225  content
//    y 226..239  hint line    what the keys do right now

#include "tdeck_avatar.h"
#include "tdeck_sound.h"

// ─────────────────────────────────────────────
//  Settings (their own NVS namespace, so a game wipe keeps them)
// ─────────────────────────────────────────────
struct TDeckPrefs {
  uint8_t theme = 0, volume = 2, bright = 12, timeout = 2, kblight = 1, portal = 0;
} tdp;
static const uint16_t TIMEOUT_S[] = { 30, 60, 120, 300, 0 };
static const char*    TIMEOUT_TXT[] = { "30 s", "1 min", "2 min", "5 min", "never" };
static const uint8_t  KB_DUTY[] = { 0, 60, 200 };
static const char*    KB_TXT[] = { "OFF", "LOW", "HIGH" };
static const char*    VOL_TXT[] = { "OFF", "LOW", "MEDIUM", "HIGH" };

void tdeckLoadPrefs() {
  preferences.begin("c32-tdeck", true);
  tdp.theme   = preferences.getUChar("theme", 0);
  tdp.volume  = preferences.getUChar("vol", 2);
  tdp.bright  = preferences.getUChar("bright", 12);
  tdp.timeout = preferences.getUChar("tmo", 2);
  tdp.kblight = preferences.getUChar("kbl", 1);
  tdp.portal  = preferences.getUChar("portal", 0);
  preferences.end();
  if (tdp.theme > 3) tdp.theme = 0;
  if (tdp.volume > 3) tdp.volume = 2;
  if (tdp.bright < 1 || tdp.bright > 16) tdp.bright = 12;
  if (tdp.timeout > 4) tdp.timeout = 2;
  if (tdp.kblight > 2) tdp.kblight = 1;
  sndVolume = tdp.volume;
}
void tdeckSavePrefs() {
  preferences.begin("c32-tdeck", false);
  preferences.putUChar("theme", tdp.theme);
  preferences.putUChar("vol", tdp.volume);
  preferences.putUChar("bright", tdp.bright);
  preferences.putUChar("tmo", tdp.timeout);
  preferences.putUChar("kbl", tdp.kblight);
  preferences.putUChar("portal", tdp.portal);
  preferences.end();
}
// A breach whose countdown started is resolved by a verdict or not at all:
// the target is written here when the countdown starts and cleared when the
// verdict is applied. Found set at boot, the device was switched off
// mid-breach, and that is a miss (see tdeckAppBegin).
// True while a breach run is counting down or the ball is moving: the sketch
// holds back anything slow (flash writes, the web portal) until it is over.
bool breachLive();

void breachPendingSave(uint32_t id) {
  preferences.begin("c32-tdeck", false);
  preferences.putULong("brPend", id);
  preferences.end();
}
static uint32_t breachPendingTake() {
  preferences.begin("c32-tdeck", false);
  uint32_t id = preferences.getULong("brPend", 0);
  if (id) preferences.putULong("brPend", 0);
  preferences.end();
  return id;
}

// The web portal and its Wi-Fi are off unless asked for: the T-Deck does not
// need a phone, and the access point is the biggest drain on the battery.
bool tdeckPortalEnabled() { return tdp.portal != 0; }

// ─────────────────────────────────────────────
//  Palette
// ─────────────────────────────────────────────
struct Pal { const char* name; uint16_t bg, panel, line, fg, dim, accent, good, bad, warn; };
#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
static const Pal PALS[4] = {
  { "PHOSPHOR", RGB565(4, 9, 8),   RGB565(10, 22, 18),  RGB565(28, 60, 44),  RGB565(190, 250, 205), RGB565(92, 140, 108),
                RGB565(60, 255, 120), RGB565(60, 255, 120), RGB565(255, 74, 90), RGB565(255, 195, 74) },
  { "AMBER",    RGB565(12, 7, 0),  RGB565(26, 16, 2),   RGB565(64, 42, 8),   RGB565(255, 222, 170), RGB565(160, 118, 64),
                RGB565(255, 176, 0),  RGB565(170, 230, 90), RGB565(255, 80, 60), RGB565(255, 210, 90) },
  { "ICE",      RGB565(3, 6, 14),  RGB565(11, 19, 34),  RGB565(30, 46, 76),  RGB565(208, 230, 255), RGB565(110, 140, 180),
                RGB565(110, 200, 255), RGB565(90, 240, 170), RGB565(255, 90, 110), RGB565(255, 200, 90) },
  { "PAPER",    RGB565(232, 230, 220), RGB565(246, 244, 236), RGB565(184, 180, 168), RGB565(24, 24, 24), RGB565(110, 108, 98),
                RGB565(10, 122, 58), RGB565(10, 122, 58), RGB565(192, 16, 32), RGB565(160, 96, 0) },
};
#define PAL (PALS[tdp.theme])

static uint16_t facCol(char f) {
  if (f == 'B' && tdp.theme == 3) return RGB565(90, 40, 160);   // violet reads on paper
  if (f == 'W' && tdp.theme == 3) return RGB565(60, 60, 60);
  return factionColour(f);
}
static const char* facName(char f) {
  switch (f) { case 'B': return "BLACK"; case 'W': return "WHITE";
               case 'R': return "RED";   case 'G': return "GREEN"; default: return "?"; }
}

// ─────────────────────────────────────────────
//  Drawing
// ─────────────────────────────────────────────
//  One full-screen off-screen canvas (150 KB, in PSRAM), pushed whole when a
//  screen changes (~30 ms) and in small rectangles while something animates.
//  Nothing is ever drawn straight to the glass, so nothing flickers.
GFXcanvas16* scr = nullptr;
#define GR (*scr)

static void pushRect(int x, int y, int w, int h) {
  if (!tft || !scr || !tdeckScreenOn) return;
  if (x < 0) { w += x; x = 0; } if (y < 0) { h += y; y = 0; }
  if (x + w > TDECK_W) w = TDECK_W - x;
  if (y + h > TDECK_H) h = TDECK_H - y;
  if (w <= 0 || h <= 0) return;
  uint16_t* buf = scr->getBuffer();
  tft->startWrite();
  tft->setAddrWindow(x, y, w, h);
  for (int j = 0; j < h; j++) tft->writePixels(buf + (y + j) * TDECK_W + x, w);
  tft->endWrite();
}
static void pushAll() { pushRect(0, 0, TDECK_W, TDECK_H); }

// The built-in font is 7-bit ASCII. The game's messages use em dashes, and
// anything can arrive over the air, so text is folded to ASCII on the way to
// the glass: dashes become '-', any other UTF-8 sequence one '?'.
static String ascii(const String& s) {
  String o;
  int L = s.length();
  for (int i = 0; i < L; i++) {
    uint8_t c = (uint8_t)s[i];
    if (c < 0x80) { o += (char)c; continue; }
    if (c == 0xE2 && i + 2 < L && (uint8_t)s[i + 1] == 0x80 &&
        ((uint8_t)s[i + 2] == 0x93 || (uint8_t)s[i + 2] == 0x94)) { o += '-'; i += 2; continue; }
    if      ((c & 0xE0) == 0xC0) i += 1;
    else if ((c & 0xF0) == 0xE0) i += 2;
    else if ((c & 0xF8) == 0xF0) i += 3;
    o += '?';
  }
  return o;
}
static void txt(int x, int y, const String& s, uint16_t c, uint8_t size = 1) {
  GR.setTextWrap(false); GR.setTextSize(size); GR.setTextColor(c);
  GR.setCursor(x, y); GR.print(ascii(s));
}
static int  txtW(const String& s, uint8_t size = 1) { return (int)ascii(s).length() * 6 * size; }
static void txtC(int cx, int y, const String& s, uint16_t c, uint8_t size = 1) { txt(cx - txtW(s, size) / 2, y, s, c, size); }
static void txtR(int rx, int y, const String& s, uint16_t c, uint8_t size = 1) { txt(rx - txtW(s, size), y, s, c, size); }
static String clip(const String& s0, int cols) {
  String s = ascii(s0);
  if ((int)s.length() <= cols) return s;
  return cols > 1 ? s.substring(0, cols - 1) + "~" : s.substring(0, cols);
}
// Word wrap into up to `max` lines of `cols` characters.
static int wrap(const String& s0, int cols, String* out, int max) {
  String s = ascii(s0);
  int n = 0, i = 0, L = s.length();
  while (i < L && n < max) {
    while (i < L && s[i] == ' ') i++;
    if (i >= L) break;
    int end = i + cols;
    if (end >= L) { out[n++] = s.substring(i); break; }
    int cut = end;
    while (cut > i && s[cut] != ' ') cut--;
    if (cut == i) cut = end;
    out[n++] = s.substring(i, cut);
    i = cut;
  }
  return n;
}
static void bar(int x, int y, int w, int h, int val, int max, uint16_t c) {
  GR.drawRect(x, y, w, h, PAL.line);
  int f = max > 0 ? (int)((long)(w - 2) * (val < 0 ? 0 : val > max ? max : val) / max) : 0;
  if (f > 0) GR.fillRect(x + 1, y + 1, f, h - 2, c);
}
static void chip(int x, int y, const String& s, uint16_t c, bool filled = true) {
  int w = txtW(s) + 6;
  if (filled) { GR.fillRoundRect(x, y, w, 11, 3, c); txt(x + 3, y + 2, s, PAL.bg); }
  else        { GR.drawRoundRect(x, y, w, 11, 3, c); txt(x + 3, y + 2, s, c); }
}
static void sigBars(int x, int y, int n, uint16_t c) {       // 4 bars, 10 px tall
  for (int i = 0; i < 4; i++) {
    int h = 3 + i * 2;
    if (i < n) GR.fillRect(x + i * 3, y + 10 - h, 2, h, c);
    else       GR.drawFastHLine(x + i * 3, y + 9, 2, PAL.line);
  }
}
static void panel(int x, int y, int w, int h, uint16_t border) {
  GR.fillRoundRect(x, y, w, h, 5, PAL.panel);
  GR.drawRoundRect(x, y, w, h, 5, border);
}
static String fmtAge(uint32_t ms) {
  uint32_t s = ms / 1000;
  if (s < 60)   return String(s) + "s";
  if (s < 3600) return String(s / 60) + "m";
  return String(s / 3600) + "h";
}
static String fmtLeft(unsigned long ms) {
  unsigned long m = ms / 60000;
  if (m >= 1440) return String(m / 1440) + "d " + String((m % 1440) / 60) + "h";
  if (m >= 60)   return String(m / 60) + "h " + String(m % 60) + "m";
  return String(m ? m : 1) + "m";
}

// ─────────────────────────────────────────────
//  State
// ─────────────────────────────────────────────
enum Tab   : uint8_t { T_HOME, T_RADAR, T_INBOX, T_SKILLS, T_LOG, T_OPTS, T_COUNT };
enum Modal : uint8_t { M_NONE, M_SETUP, M_DOSSIER, M_RECON, M_COMPOSE, M_CONFIRM,
                       M_CARD, M_TEXT, M_DIAG, M_BREACH, M_ABOUT, M_BREACHGAME };
static const char* TAB_NAME[T_COUNT] = { "HOME", "RADAR", "INBOX", "SKILLS", "LOG", "OPTIONS" };
static const char  TAB_KEY[T_COUNT]  = { 'h', 'r', 'i', 'k', 'l', 'o' };

struct UI {
  uint8_t  tab = T_HOME, modal = M_NONE;
  int      sel[T_COUNT] = {0};
  int      scroll[T_COUNT] = {0};
  bool     dirty = true;
  uint32_t lastInput = 0, lastFull = 0, lastAnim = 0;
  // toast
  String   toast; uint8_t toastTone = 0; uint32_t toastUntil = 0;
  // speech bubble
  String   bubble; uint32_t bubbleAt = 0;
  // dossier target (0 = none); TRAINING_ID for the dummy; echo flag
  uint32_t target = 0; bool targetEcho = false; uint32_t hackArmUntil = 0;
  // card
  String   cardTitle, cardLine[4]; uint8_t cardTone = 0; uint32_t cardUntil = 0;
  uint8_t  cardKind = 0; uint32_t cardReply = 0; uint8_t cardReturn = 0;
  // radar selection follows the node, not the row number
  uint32_t selId = 0; bool selEcho = false;
  // confirm / text entry
  uint8_t  confirmWhat = 0; String confirmMsg;
  uint8_t  textWhat = 0; String textPrompt, textBuf;
  // setup
  uint8_t  setupStep = 0, setupFac = 0;
  // breach
  String   breachName; uint32_t breachStart = 0;
  // wake / power
  bool     dimmed = false, asleep = false;
  // trackball presses already used by the breach game: their release must not
  // then also count as a "select"
  // A trackball click is a press and a release; one whose press went down
  // while the breach game was open belongs to the game, even if the release
  // comes after it has closed.
  uint32_t gameClosedAt = 0; uint32_t clickSeen = 0;
  // change detection for live screens
  uint32_t sig = 0;
} ui;

Avatar av;
enum { CARD_INFO, CARD_ARMED, CARD_WIPING, CARD_MSG, CARD_RESULT, CARD_LEVEL };
enum { CONF_HACK, CONF_CLEAR, CONF_PORTAL, CONF_SKILL };
enum { TXT_PASSWORD, TXT_WIPE, TXT_PORTAL };

// Avatar rectangles: where it lives on HOME, on a result card and in setup.
#define AV_X 6
#define AV_Y 36

void tdeckRedraw() { ui.dirty = true; }

// A battery reading is fifteen ADC samples and ~30 ms, far too slow to take
// on every frame, so it is sampled every 30 s and the screens read the cache.
int      battPct = -1;
float    battV   = 0;
uint32_t battAt  = 0;
static void battSample(bool force = false) {
  if (!force && battAt && millis() - battAt < 30000) return;
  battAt = millis();
  battV  = getBatteryVoltage();
  battPct = getBatteryPercent();
}

// Toast colours are kept as a role, not an RGB value, so a theme change while
// one is showing does not leave it in the old theme's colours.
enum { TT_GOOD, TT_BAD, TT_WARN, TT_ACCENT, TT_DIM };
static uint16_t toneCol(uint8_t t) {
  return t == TT_GOOD ? PAL.good : t == TT_BAD ? PAL.bad : t == TT_WARN ? PAL.warn
       : t == TT_ACCENT ? PAL.accent : PAL.dim;
}
static void toast(const String& s, uint8_t tone) {
  ui.toast = s; ui.toastTone = tone; ui.toastUntil = millis() + 3500; ui.dirty = true;
  Serial.printf("[TDECK] %s\n", s.c_str());
}
static void say(const String& s) { ui.bubble = s; ui.bubbleAt = millis(); ui.dirty = true; }

// Anything that should be seen wakes the screen.
static void render(uint32_t now);
static void pushAll();
static void wake() {
  ui.lastInput = millis();
  if (ui.asleep || ui.dimmed || !tdeckScreenOn) {
    bool wasAsleep = ui.asleep || ui.dimmed;
    bool wasOff = !tdeckScreenOn;
    ui.asleep = ui.dimmed = false;
    tdeckScreenPower(true);
    // Coming back from panel sleep: put a fresh frame up before the light,
    // so the stale one does not flash.
    if (wasOff && scr && scr->getBuffer()) { render(millis()); pushAll(); }
    tdeckBacklight(tdp.bright);
    av.night = false;
    // Woken with a start, unless something else is about to take over.
    if (wasAsleep && !av.reacting(millis())) {
      av.trigger(AV_R_ALERT, millis(), 500);
      static const char* WAKE[] = { "Huh? I'm up.", "Wasn't sleeping.", "...five more minutes.", "I'm here, I'm here." };
      ui.bubble = WAKE[avRange(0, 3)]; ui.bubbleAt = millis();
    }
    ui.dirty = true;
  }
}

// ── Event cards ─────────────────────────────
//  A card never tramples what the player is in the middle of. While a recon
//  run, a half-typed message, a confirmation or another card is on screen,
//  new cards wait in a short queue and come up one at a time afterwards —
//  so a hack that levels you up shows the breach, then the level-up. The
//  factory-reset cards are the exception: they are about the device itself
//  and always show at once.
struct Card { uint8_t kind, tone; String title, line[4]; uint32_t ms, reply; };
#define CARD_QUEUE 4
Card     cardQ[CARD_QUEUE];
uint8_t  cardQn = 0;

static bool cardBusy() {
  uint8_t m = ui.modal;
  return m == M_RECON || m == M_COMPOSE || m == M_TEXT || m == M_CONFIRM || m == M_SETUP || m == M_CARD ||
         m == M_BREACHGAME;
}
static void reconClose();
static void showCard(const Card& c) {
  if (ui.modal != M_CARD)
    ui.cardReturn = (ui.target && (ui.modal == M_DOSSIER || ui.modal == M_BREACH)) ? M_DOSSIER : M_NONE;
  ui.cardKind = c.kind; ui.cardTitle = c.title; ui.cardTone = c.tone;
  for (int i = 0; i < 4; i++) ui.cardLine[i] = c.line[i];
  ui.cardUntil = c.ms ? millis() + c.ms : 0;
  ui.cardReply = c.reply;
  ui.modal = M_CARD; ui.dirty = true;
}
static void openCard(uint8_t kind, const String& title, uint8_t tone, const String& l0,
                     const String& l1 = "", const String& l2 = "", const String& l3 = "",
                     uint32_t ms = 7000, uint32_t reply = 0) {
  Card c; c.kind = kind; c.tone = tone; c.title = title;
  c.line[0] = l0; c.line[1] = l1; c.line[2] = l2; c.line[3] = l3; c.ms = ms; c.reply = reply;
  wake();
  bool urgent = (kind == CARD_ARMED || kind == CARD_WIPING);
  if (urgent) {
    if (ui.modal == M_RECON) reconClose();
    showCard(c);
    return;
  }
  if (cardBusy()) {
    if (cardQn == CARD_QUEUE) { for (int i = 1; i < CARD_QUEUE; i++) cardQ[i - 1] = cardQ[i]; cardQn--; }
    cardQ[cardQn++] = c;
    return;
  }
  showCard(c);
}
static void clearUnread(uint32_t id) {
  for (int i = 0; i < knownCount; i++)
    if (!id || knownNodes[i].chip_id == id) knownNodes[i].msg_unread = false;
}
// Put the card away: back to the dossier it came from, or to the tabs.
static void dismissCard() {
  if (ui.cardKind == CARD_MSG) clearUnread(ui.cardReply);
  bool back = ui.cardReturn == M_DOSSIER && ui.target &&
              (ui.target == TRAINING_ID ? trainingActive() : (ui.targetEcho || findNode(ui.target)));
  ui.modal = back ? M_DOSSIER : M_NONE;
  if (!back) ui.target = 0;
  ui.dirty = true;
}
static void pumpCards() {
  if (!cardQn || cardBusy()) return;
  Card c = cardQ[0];
  for (int i = 1; i < cardQn; i++) cardQ[i - 1] = cardQ[i];
  cardQn--;
  showCard(c);
}

// ─────────────────────────────────────────────
//  Radar rows: the dummy, the room, then echoes
// ─────────────────────────────────────────────
struct Row { uint32_t id; KnownNode* n; bool training; bool echo; int echoIdx; };
static int radarRows(Row* rows, int max) {
  int k = 0;
  if (trainingActive() && k < max) rows[k++] = { TRAINING_ID, nullptr, true, false, -1 };
  // Room, loudest first.
  int idx[MAX_KNOWN_NODES], m = 0;
  for (int i = 0; i < knownCount; i++) idx[m++] = i;
  for (int a = 1; a < m; a++) {
    int v = idx[a], b = a - 1;
    while (b >= 0 && nodeAvgRssi(&knownNodes[idx[b]]) < nodeAvgRssi(&knownNodes[v])) { idx[b + 1] = idx[b]; b--; }
    idx[b + 1] = v;
  }
  for (int a = 0; a < m && k < max; a++) rows[k++] = { knownNodes[idx[a]].chip_id, &knownNodes[idx[a]], false, false, -1 };
  for (int e = 0; e < echoCount && k < max; e++)
    if (echoCarrierFor(echoNodes[e].chip_id)) rows[k++] = { echoNodes[e].chip_id, nullptr, false, true, e };
  return k;
}
// The highlighted radar row is remembered by node, not by position: rows are
// re-sorted by signal strength and nodes come and go, and acting on "row 2"
// after the list moved would scout or hack somebody else.
static int radarSel(Row* rows, int n) {
  int& sel = ui.sel[T_RADAR];
  if (ui.selId)
    for (int i = 0; i < n; i++)
      if (rows[i].id == ui.selId && rows[i].echo == ui.selEcho) { sel = i; break; }
  if (sel >= n) sel = n ? n - 1 : 0;
  if (sel < 0) sel = 0;
  if (n) { ui.selId = rows[sel].id; ui.selEcho = rows[sel].echo; }
  return sel;
}
static String rowName(const Row& r) {
  if (r.training) return trainScore >= RECON_T_NAME ? "TRAINING" : "UNKNOWN-0001";
  if (r.echo)     return echoDisplayName(r.id);
  return nodeDisplayName(r.id);
}

// ─────────────────────────────────────────────
//  Recon mini-game — the portal's rules, exactly
// ─────────────────────────────────────────────
//  Round n flashes n tiles out of nine (repeats allowed), each lit 320 ms,
//  one every 520 ms. Repeat them; one wrong tile ends the run; there is no
//  clock on input. Score = the longest round repeated, 0..10. Each cleared
//  round reveals its tier at once. A backdoored node skips the game and
//  replays every tier 220 ms apart.
enum { RC_LINK, RC_READY, RC_WATCH, RC_INPUT, RC_BAD, RC_NEXT, RC_BACKDOOR, RC_DONE, RC_NOLINK };
static const uint8_t RC_TIERS[7] = { RECON_T_NAME, RECON_T_FACTION, RECON_T_LEVEL, RECON_T_BRUTE,
                                     RECON_T_STEALTH, RECON_T_FIREWALL, RECON_T_PWNED };
static const char* RC_LABEL[7] = { "CODENAME", "FACTION", "LEVEL", "BRUTE", "STEALTH", "FIREWALL", "BACKDOOR" };
// Keys on the grid: the T-Deck prints 1-9 on exactly these caps.
static const char RC_KEYS[9] = { 'w', 'e', 'r', 's', 'd', 'f', 'z', 'x', 'c' };

struct Recon {
  uint32_t target = 0;
  uint8_t  phase = RC_LINK;
  uint8_t  order[RECON_MAX_SEQ];
  int      len = 0, best = 0, pos = 0, cursor = 4, bdStep = 0;
  uint32_t t0 = 0, deadline = 0, flashUntil = 0;
  int      flash = -1; bool flashBad = false;
  String   field[7];
  String   msg;
  bool     ended = false, pwned = false;
} rc;

static void reconSeed() {
  for (auto& f : rc.field) f = "";
  if (rc.target == TRAINING_ID) {
    int t = trainScore;
    if (t >= RECON_T_NAME)     rc.field[0] = "TRAINING";
    if (t >= RECON_T_FACTION)  rc.field[1] = "GREEN";
    if (t >= RECON_T_LEVEL)    rc.field[2] = "1";
    if (t >= RECON_T_BRUTE)    rc.field[3] = String(TRAIN_BRUTE);
    if (t >= RECON_T_STEALTH)  rc.field[4] = String(TRAIN_STEALTH);
    if (t >= RECON_T_FIREWALL) rc.field[5] = String(TRAIN_FIREWALL);
    return;
  }
  KnownNode* n = findNode(rc.target);
  if (!n) return;
  String nm = nodeDisplayName(n->chip_id);
  if (n->intel >= RECON_T_NAME && !nm.startsWith("UNKNOWN")) rc.field[0] = nm;
  if (reconKnows(n, RECON_T_FACTION)) rc.field[1] = facName(n->faction);
  if (reconKnows(n, RECON_T_LEVEL))   rc.field[2] = String(n->level);
  if (nodeKnownBrute(n) >= 0)    rc.field[3] = String(nodeKnownBrute(n));
  if (nodeKnownStealth(n) >= 0)  rc.field[4] = String(nodeKnownStealth(n));
  if (nodeKnownFirewall(n) >= 0) rc.field[5] = String(nodeKnownFirewall(n));
  if (n->pwned) rc.field[6] = "OPEN";
}

static void reconStart(uint32_t id) {
  ActResult r = actReconOpen(id);
  if (r.code != 200) { toast(r.msg, TT_BAD); sfx(SFX_ERR); return; }
  rc = Recon();
  rc.target = id;
  ui.target = id; ui.targetEcho = false;       // closing the run lands on their dossier
  KnownNode* n = findNode(id);
  rc.pwned = n && n->pwned;
  reconSeed();
  rc.phase = (reconProbe.state == RECON_PROBE_READY && reconProbe.target == id) ? RC_READY : RC_LINK;
  rc.deadline = millis() + RECON_PROBE_MS + 3000;
  if (rc.phase == RC_READY && rc.pwned) { rc.phase = RC_BACKDOOR; rc.t0 = millis(); }
  ui.modal = M_RECON; ui.dirty = true;
  av.trigger(AV_R_SCAN, millis(), 2500);
  sfx(SFX_CLICK);
}

static void reconReveal(int t) {
  // With the phone portal on, someone could open a probe on another node from
  // there; never let this run reveal (or charge) theirs.
  if (reconProbe.target != rc.target) return;
  ReconReveal rv;
  if (!actReconReveal(t, &rv)) return;
  for (int i = 0; i < 7; i++) if (RC_TIERS[i] == rv.tier && rv.field) {
    String v = rv.value;
    if (rv.tier == RECON_T_FACTION && v.length()) v = facName(v[0]);
    rc.field[i] = v;
  }
}

static void reconNextRound() {
  rc.order[rc.len++] = (uint8_t)random(0, 9);
  // The portal's first flash comes one full step (520 ms) after the round
  // starts; the same here, so the rhythm is identical.
  rc.pos = 0; rc.phase = RC_WATCH; rc.t0 = millis() + 520;   // cursor stays where it was
  ui.dirty = true;
}

static void reconFinish(const String& why) {
  rc.phase = RC_DONE;
  String m = why + " Sequence " + String(rc.best) + " = +" +
             String(rc.best * 3) + "% breach window.";
  for (int i = 0; i < 7; i++) if (RC_TIERS[i] > rc.best) {
    m += " Round " + String(RC_TIERS[i]) + " was their " + String(RC_LABEL[i]) + ".";
    break;
  }
  rc.msg = m;
  rc.t0 = millis();
  ui.dirty = true;
}

// Closing always commits, exactly once — the portal's rule.
static void reconClose() {
  if (!rc.ended) {
    rc.ended = true;
    // Nothing was played (backed out before round one, or no link): close the
    // probe without logging a run that never happened.
    bool played = rc.best > 0 || rc.len > 0 || rc.phase == RC_DONE;
    if (played) {
      ActResult r = actReconEnd(rc.target, rc.best);
      if (rc.best > 0) toast(r.msg, r.code == 200 ? TT_GOOD : TT_BAD);
    } else {
      reconProbe.state = RECON_PROBE_IDLE;
    }
  }
  ui.modal = (ui.target ? M_DOSSIER : M_NONE);
  ui.dirty = true;
}

static void reconTap(int i) {
  if (rc.phase != RC_INPUT || i < 0 || i > 8) return;
  rc.cursor = i;
  if (i != rc.order[rc.pos]) {
    rc.flash = i; rc.flashBad = true; rc.flashUntil = millis() + 500;
    rc.phase = RC_BAD; rc.t0 = millis();
    sfx(SFX_ERR); av.trigger(AV_R_LOSE, millis(), 1200);
    ui.dirty = true;
    return;
  }
  rc.flash = i; rc.flashBad = false; rc.flashUntil = millis() + 110;
  sfx(SFX_TILE0);
  if (++rc.pos >= rc.len) {
    rc.best = rc.len;
    reconReveal(rc.best);
    if (rc.best >= RECON_MAX_SEQ) {
      sfx(SFX_PERFECT); av.trigger(AV_R_LEVELUP, millis(), 3000);
      reconFinish("Perfect run. Backdoor open.");
    } else {
      bool tier = false;
      for (uint8_t t : RC_TIERS) if (t == rc.best) tier = true;
      if (tier) sfx(SFX_ROUND);
      rc.phase = RC_NEXT; rc.t0 = millis();
    }
  }
  ui.dirty = true;
}

// Called every frame while the recon modal is up.
static void reconTick(uint32_t now) {
  switch (rc.phase) {
    case RC_LINK:
      if (reconProbe.target == rc.target && reconProbe.state == RECON_PROBE_READY) {
        rc.phase = rc.pwned ? RC_BACKDOOR : RC_READY; rc.t0 = now; ui.dirty = true;
        sfx(SFX_ROUND);
      } else if ((reconProbe.state == RECON_PROBE_FAILED && reconProbe.target == rc.target) ||
                 (int32_t)(now - rc.deadline) > 0) {
        rc.phase = RC_NOLINK; rc.msg = "No response — out of range?"; ui.dirty = true; sfx(SFX_ERR);
      } else if ((now / 250) != ((now - 40) / 250)) ui.dirty = true;   // spinner
      break;
    case RC_WATCH: {
      int32_t e = (int32_t)(now - rc.t0);
      int idx = e < 0 ? -1 : e / 520;
      int lit = (idx >= 0 && idx < rc.len && (e % 520) < 320) ? rc.order[idx] : -1;
      if (lit != rc.flash) {
        rc.flash = lit; rc.flashBad = false; ui.dirty = true;
        // One neutral tick for every tile. A different note per tile (Simon)
        // would make the sequence easier to remember than on the phone,
        // which plays no sound at all — an unfair edge in odds and backdoors.
        if (lit >= 0) sfx(SFX_TILE0);
      }
      if (idx >= rc.len) { rc.phase = RC_INPUT; rc.flash = -1; ui.dirty = true; sndPlay(1760, 25); }
      break;
    }
    case RC_INPUT:
      if (rc.flash >= 0 && (int32_t)(now - rc.flashUntil) > 0) { rc.flash = -1; ui.dirty = true; }
      break;
    case RC_BAD:
      if ((int32_t)(now - rc.flashUntil) > 0) { rc.flash = -1; reconFinish("Wrong tile."); }
      break;
    case RC_NEXT:
      if (rc.flash >= 0 && (int32_t)(now - rc.flashUntil) > 0) { rc.flash = -1; ui.dirty = true; }
      if ((int32_t)(now - rc.t0) > 450) reconNextRound();
      break;
    case RC_BACKDOOR:
      if ((int32_t)(now - rc.t0) > 220) {
        rc.t0 = now;
        if (rc.bdStep < 7) { reconReveal(RC_TIERS[rc.bdStep++]); sfx(SFX_TILE0); ui.dirty = true; }
        else { rc.best = RECON_MAX_SEQ; reconFinish("Backdoor still open."); }
      }
      break;
  }
}

// ─────────────────────────────────────────────
//  Compose
// ─────────────────────────────────────────────
//  Recipients: everyone in range, then anyone an echo says a neighbour can
//  reach. In range = a message now; out of range = mail that travels.
struct Compose { uint32_t to = 0; String text; } cm;

static int composeTargets(uint32_t* ids, bool* echo, int max) {
  int k = 0;
  for (int i = 0; i < knownCount && k < max; i++) { ids[k] = knownNodes[i].chip_id; echo[k++] = false; }
  for (int e = 0; e < echoCount && k < max; e++)
    if (echoCarrierFor(echoNodes[e].chip_id) && !findNode(echoNodes[e].chip_id)) {
      ids[k] = echoNodes[e].chip_id; echo[k++] = true;
    }
  return k;
}
static void composeOpen(uint32_t to) {
  cm.to = to; cm.text = "";
  uint32_t ids[MAX_KNOWN_NODES + ECHO_MAX_NODES]; bool ec[MAX_KNOWN_NODES + ECHO_MAX_NODES];
  int k = composeTargets(ids, ec, MAX_KNOWN_NODES + ECHO_MAX_NODES);
  bool ok = false;
  for (int i = 0; i < k; i++) if (ids[i] == to) ok = true;
  if (!ok && to) {
    // A reply to someone nobody can reach must not quietly go to somebody else.
    toast("Can't reach " + nodeDisplayName(to) + " right now", TT_BAD); sfx(SFX_ERR);
    return;
  }
  if (!ok) cm.to = k ? ids[0] : 0;
  clearUnread(cm.to);
  ui.modal = M_COMPOSE; ui.dirty = true;
}
static void composeStep(int d) {
  uint32_t ids[MAX_KNOWN_NODES + ECHO_MAX_NODES]; bool ec[MAX_KNOWN_NODES + ECHO_MAX_NODES];
  int k = composeTargets(ids, ec, MAX_KNOWN_NODES + ECHO_MAX_NODES);
  if (!k) { cm.to = 0; return; }
  int cur = 0;
  for (int i = 0; i < k; i++) if (ids[i] == cm.to) cur = i;
  cm.to = ids[(cur + k + d) % k];
  ui.dirty = true; sfx(SFX_NAV);
}
static void composeSend() {
  if (!cm.to) { toast("Nobody to send to yet", TT_BAD); sfx(SFX_ERR); return; }
  ActResult r = findNode(cm.to) ? actMsg(cm.to, cm.text) : actMail(cm.to, cm.text);
  if (r.code != 200) { toast(r.msg, TT_BAD); sfx(SFX_ERR); return; }
  sfx(SFX_SEND);
  toast(findNode(cm.to) ? "Sent to " + nodeDisplayName(cm.to) : r.msg, TT_GOOD);
  ui.modal = ui.target ? M_DOSSIER : M_NONE; ui.dirty = true;
}

// ─────────────────────────────────────────────
//  Hacking from the dossier
// ─────────────────────────────────────────────
//  A hack is played as the breach game (tdeck_breach.h). What it will look
//  like against a target — the gap and the ball's speed — is worked out here
//  so the dossier and the radar can show it before you commit.
struct BreachTune {
  int      zoneW, coreW;       // px: the gap, and its dead centre
  int      speed;              // px/s
  uint32_t periodMs;           // one sweep there and back
  int      maxSweeps;          // wall-to-wall legs before it times out
  int      windowMs;           // how long the ball is inside the gap per pass
};
BreachTune breachTune(int aB, int aS, int aF, float dB, float dS, float dF, int recon, bool practice);
#define BR_LEN  280                              // px: the bar the ball runs along
#define BR_EDGE 64                               // px: the gap keeps this clear of each end
// How much each of the target's stats weighs in the breach (the exponents in
// breachTune): STEALTH narrows the gap, FIREWALL speeds the ball, BRUTE
// counters your stealth.
#define BRT_E_W  1.75f                           // your BRUTE vs their STEALTH
#define BRT_E_T  1.05f                           // your STEALTH vs their BRUTE
#define BRT_E_V  1.40f                           // their FIREWALL vs your FIREWALL

// What the attacker knows about the target, and so what the game is played
// against. A stat the recon has not reached yet is assumed to be the worst
// it could possibly be: the target's level is public (every beacon carries
// it), so their points are known in total, and whatever is not yet revealed
// is assumed spread the way that would make this breach hardest (within what
// their faction, once known, guarantees). So scouting
// can only ever make a breach easier or leave it as it is — never harder —
// and skipping recon is never a way around a strong target.
struct BreachSpec {
  BreachTune tu;
  float dB = 0, dS = 0, dF = 0;                  // what the game uses
  bool  knowB = false, knowS = false, knowF = false;
  int   recon = 0;
};
static BreachSpec breachSpecFor(uint32_t id) {
  BreachSpec s;
  bool practice = id == TRAINING_ID;
  int total = 0, tb = -1, ts = -1, tf = -1;
  char fac = '?';
  if (practice) {
    int k = trainScore;
    total = TRAIN_BRUTE + TRAIN_STEALTH + TRAIN_FIREWALL;
    if (k >= RECON_T_BRUTE)    tb = TRAIN_BRUTE;
    if (k >= RECON_T_STEALTH)  ts = TRAIN_STEALTH;
    if (k >= RECON_T_FIREWALL) tf = TRAIN_FIREWALL;
    s.recon = k;
  } else {
    KnownNode* n = findNode(id);
    int lvl = n && n->level > 0 ? n->level : myLevel + 2;
    if (lvl > MAX_LEVEL) lvl = MAX_LEVEL;
    total = lvl + 2;                             // level-1 earned + 3 from the faction
    if (n) { tb = nodeKnownBrute(n); ts = nodeKnownStealth(n); tf = nodeKnownFirewall(n);
             fac = nodeKnownFaction(n); s.recon = n->recon_score; }
  }
  s.knowB = tb >= 0; s.knowS = ts >= 0; s.knowF = tf >= 0;
  // Once recon has their faction, its starting points are a floor under each
  // stat still hidden (BLACK +3 BRUTE, WHITE +3 FIREWALL, RED +3 STEALTH,
  // GREEN +1 each).
  int fl[3] = { 0, 0, 0 };
  if (fac == 'B') fl[0] = 3; else if (fac == 'R') fl[1] = 3;
  else if (fac == 'W') fl[2] = 3; else if (fac == 'G') fl[0] = fl[1] = fl[2] = 1;
  float x[3] = { (float)(s.knowB ? tb : fl[0]), (float)(s.knowS ? ts : fl[1]), (float)(s.knowF ? tf : fl[2]) };
  float R = (float)total - x[0] - x[1] - x[2];  // points not accounted for yet
  if (R < 0) R = 0;
  // Worst case: breachTune is linear in each stat inside its exponent, so
  // the hardest spread puts every unaccounted point on the unknown stat that
  // weighs most — STEALTH (1.75), else FIREWALL (1.4), else BRUTE (1.05).
  if (!s.knowS) x[1] += R;
  else if (!s.knowF) x[2] += R;
  else if (!s.knowB) x[0] += R;
  s.dB = x[0]; s.dS = x[1]; s.dF = x[2];
  s.tu = breachTune(skillBrute, skillStealth, skillFirewall, s.dB, s.dS, s.dF, s.recon, practice);
  return s;
}
static int breachWindowMs(const BreachTune& t) { return t.windowMs; }
// Bands of that window, for a typical player (~35 ms of timing spread):
// EASY ≥ ~75% hits, FAIR ~60-75%, HARD ~45-60%, BRUTAL below.
static const char* breachGrade(int windowMs) {
  return windowMs >= 91 ? "EASY" : windowMs >= 65 ? "FAIR" : windowMs >= 45 ? "HARD" : "BRUTAL";
}
static uint16_t breachGradeCol(int windowMs) {
  return windowMs >= 91 ? PAL.good : windowMs >= 65 ? PAL.accent : windowMs >= 45 ? PAL.warn : PAL.bad;
}

// ─────────────────────────────────────────────
//  Screens
// ─────────────────────────────────────────────
static void drawStatus() {
  GR.fillRect(0, 0, TDECK_W, 16, PAL.panel);
  GR.drawFastHLine(0, 15, TDECK_W, PAL.line);
  if (!gameConfigured()) { txt(4, 4, "CYPHER32", PAL.accent); txtR(316, 4, "SETUP", PAL.dim); return; }
  char f = myFaction.charAt(0);
  GR.fillRect(3, 3, 4, 10, facCol(f));
  String who = myName + "  LV" + (myLevel >= MAX_LEVEL ? String("MAX") : String(myLevel));
  txt(10, 4, who, PAL.fg);
  int xw = 60, xx = 12 + txtW(who) + 6;
  bar(xx, 5, xw, 6, myXP, xpForNextLevel(), PAL.accent);
  // right side, right to left: battery, mail, nodes, radio
  int x = 316;
  int bat = battPct;
  String b = bat < 0 ? "USB" : String(bat) + "%";
  txtR(x, 4, b, bat >= 0 && bat < 15 ? PAL.bad : PAL.dim); x -= txtW(b) + 16;
  GR.drawRect(x, 4, 12, 8, PAL.dim); GR.fillRect(x + 12, 6, 2, 4, PAL.dim);
  if (bat != 0) GR.fillRect(x + 2, 6, bat < 0 ? 8 : (8 * bat) / 100, 4, bat >= 0 && bat < 15 ? PAL.bad : PAL.good);
  x -= 8;
  int unread = 0;
  for (int i = 0; i < knownCount; i++) if (knownNodes[i].msg_unread) unread++;
  if (unread) {
    x -= 14; GR.drawRect(x, 4, 12, 8, PAL.warn); GR.drawLine(x, 4, x + 6, 8, PAL.warn); GR.drawLine(x + 11, 4, x + 6, 8, PAL.warn);
    x -= 4;
  }
  String nn = String(knownCount) + " near";
  txtR(x, 4, nn, PAL.fg); x -= txtW(nn) + 12;
  uint16_t rcol = !loraReady ? PAL.bad : (loraActionPending() || hackInFlight) ? PAL.warn : PAL.accent;
  GR.drawCircle(x + 4, 8, 4, rcol);
  GR.fillCircle(x + 4, 8, 1, rcol);
}

static void drawTabs() {
  GR.fillRect(0, 16, TDECK_W, 14, PAL.bg);
  int w = TDECK_W / T_COUNT;
  for (int i = 0; i < T_COUNT; i++) {
    bool on = (ui.tab == i && ui.modal == M_NONE);
    bool act = (ui.tab == i);
    int x = i * w;
    if (on) GR.fillRect(x + 1, 17, w - 2, 12, PAL.accent);
    else if (act) GR.drawRect(x + 1, 17, w - 2, 12, PAL.accent);
    String s = TAB_NAME[i];
    int tx = x + (w - txtW(s)) / 2;
    txt(tx, 19, s, on ? PAL.bg : act ? PAL.accent : PAL.dim);
    // Underline the hotkey letter.
    int k = s.indexOf((char)toupper(TAB_KEY[i]));
    if (k >= 0) GR.drawFastHLine(tx + k * 6, 27, 5, on ? PAL.bg : PAL.dim);
  }
  // Badge: unspent skill points.
  if (skillPoints > 0 && gameConfigured()) GR.fillCircle(3 * w + w - 6, 19, 3, PAL.warn);
}

static void drawHint(const String& s) {
  GR.fillRect(0, 226, TDECK_W, 14, PAL.panel);
  GR.drawFastHLine(0, 226, TDECK_W, PAL.line);
  // The radio action in flight outranks the key help: it is what the
  // player is waiting on.
  String act = "";
  if (hackInFlight || loraActionPending()) {
    act = loraActionLabel + ": " + loraActionText();
    if (loraActionTries) act += " " + String(loraActionTries) + "/" + String(TX_MAX_TRIES);
  }
  if (act.length()) { txt(4, 230, clip(act, 52), PAL.warn); return; }
  txt(4, 230, clip(s, 52), PAL.dim);
}

// A toast floats just above the hint line, so the key help stays readable.
static void drawToast() {
  if (!ui.toastUntil || (int32_t)(millis() - ui.toastUntil) > 0) return;
  String s = clip(ui.toast, 50);
  int w = txtW(s) + 10, x = (TDECK_W - w) / 2;
  GR.fillRoundRect(x, 211, w, 14, 4, toneCol(ui.toastTone));
  txt(x + 5, 214, s, PAL.bg);
}

// Speech bubble to the right of the avatar.
static void drawBubble(int x, int y, int w, const String& s, uint16_t border) {
  String L[4];
  int n = wrap(s, (w - 12) / 6, L, 4);
  int h = n * 11 + 10;
  panel(x, y, w, h, border);
  GR.fillTriangle(x, y + 12, x - 7, y + 16, x, y + 20, PAL.panel);
  GR.drawLine(x, y + 12, x - 7, y + 16, border); GR.drawLine(x - 7, y + 16, x, y + 20, border);
  for (int i = 0; i < n; i++) txt(x + 6, y + 6 + i * 11, L[i], PAL.fg);
}

// The avatar's canvas, copied into the screen canvas.
static void blitAvatar(int x, int y) {
  if (!av.cv || !av.cv->getBuffer()) return;
  uint16_t* s = av.cv->getBuffer();
  uint16_t* d = scr->getBuffer();
  for (int j = 0; j < AV_H; j++)
    memcpy(d + (y + j) * TDECK_W + x, s + j * AV_W, AV_W * 2);
}

// What should the player do next? One line, always actionable.
// What should the player do next? One line, always actionable — and ENTER
// on HOME does it, so a new player only ever needs one key.
enum { NX_SKILLS, NX_DOSSIER, NX_WAIT, NX_COMPOSE };
struct NextMove { String text; uint8_t kind; uint32_t id; };
static NextMove nextMove() {
  if (skillPoints > 0)
    return { String(skillPoints) + " skill point" + (skillPoints > 1 ? "s" : "") + " to spend.", NX_SKILLS, 0 };
  if (trainingActive() && trainScore == 0 && trainRecon < 3)
    return { "Practice first: scout the dummy on the radar.", NX_DOSSIER, TRAINING_ID };
  if (trainingActive())
    return { "The dummy is scouted. Breach it: open it, X twice.", NX_DOSSIER, TRAINING_ID };
  if (knownCount == 0)
    return { "Nobody in range. Walk around, I'll shout when I hear someone.", NX_WAIT, 0 };
  for (int i = 0; i < knownCount; i++) {
    KnownNode* n = &knownNodes[i];
    if (n->intel < RECON_T_NAME && nodeCanRecon(n))
      return { "Unknown signal, " + String(nodeProximity(n)) + ". Scout it.", NX_DOSSIER, n->chip_id };
  }
  for (int i = 0; i < knownCount; i++) {
    KnownNode* n = &knownNodes[i];
    String nid = chipIdStr(n->chip_id);
    if (nodeOdds(n) >= 0 && nodeCanHack(n) && !recentlyHacked(nid) && !recentlyFailed(nid))
      return { nodeDisplayName(n->chip_id) + " is readable: " +
               breachGrade(breachWindowMs(breachSpecFor(n->chip_id).tu)) + " breach. Go?", NX_DOSSIER, n->chip_id };
  }
  for (int i = 0; i < knownCount; i++) {
    KnownNode* n = &knownNodes[i];
    if (nodeCanRecon(n) && n->recon_score < RECON_T_FIREWALL)
      return { "Read " + nodeDisplayName(n->chip_id) + " deeper: recon widens the gap.", NX_DOSSIER, n->chip_id };
  }
  return { "All quiet. Say something to someone.", NX_COMPOSE, 0 };
}
static String nextStep() { return nextMove().text; }

// Moods come in bands; the bubble changes when the band does.
static int moodBand() { return cyMood >= 3 ? 2 : cyMood >= 1 ? 1 : cyMood >= -1 ? 0 : cyMood >= -3 ? -1 : -2; }
static const char* firstRunLine() {
  static const char* L[] = { "New here? Poke the dummy.", "Let's learn the ropes.", "Fresh start. Let's hunt." };
  return L[(millis() / 30000) % 3];
}

static void drawHome(uint32_t now) {
  blitAvatar(AV_X, AV_Y);
  // Bubble.
  static int lastBand = 99;
  if (!ui.bubble.length() || (int32_t)(now - ui.bubbleAt) > 25000 || moodBand() != lastBand) {
    lastBand = moodBand();
    ui.bubble = (statWon + statLost == 0 && trainingActive()) ? String(firstRunLine()) : String(getIdleBubble());
    ui.bubbleAt = now;
  }
  drawBubble(154, 38, 160, ui.bubble, facCol(myFaction.charAt(0)));
  // Situation, just under the bubble.
  int y = 38 + 16 + 11 * min(4, (int)(ui.bubble.length() / 25 + 1)) + 8;
  if (y < 80) y = 80;
  int unscouted = 0;
  for (int i = 0; i < knownCount; i++) if (knownNodes[i].intel < RECON_T_NAME) unscouted++;
  txt(150, y, String(knownCount) + " in range" + (unscouted ? "  (" + String(unscouted) + " unknown)" : ""), PAL.fg); y += 12;
  int mp = mailPending();
  if (mp) { txt(150, y, String(mp) + " mail waiting to go", PAL.dim); y += 12; }
  if (msgLogCount) {
    const MsgLogEntry* e = msgLogAt(0);
    if (e) { txt(150, y, clip("Last msg: " + nodeDisplayName(e->from), 27), PAL.dim); y += 12; }
  }
  // The next move, framed: it is the most important thing on this screen.
  String L[3];
  int n = wrap(nextStep(), 25, L, 3);
  GR.drawRoundRect(147, 128, 168, 44, 4, PAL.accent);
  txt(152, 133, ">", PAL.accent);
  for (int i = 0; i < n; i++) txt(162, 133 + i * 11, L[i], PAL.accent);
  // XP and skills along the bottom.
  int by = 176;
  txt(8, by, "XP " + String(myXP) + "/" + String(xpForNextLevel()), PAL.fg);
  if (skillPoints) txtR(312, by, "SP " + String(skillPoints) + "  press K", PAL.warn);
  bar(8, by + 11, 304, 7, myXP, xpForNextLevel(), PAL.accent);
  const char* SL[3] = { "BRUTE", "STEALTH", "FIREWALL" };
  int SV[3] = { skillBrute, skillStealth, skillFirewall };
  for (int i = 0; i < 3; i++) {
    int x = 8 + i * 103;
    txt(x, by + 24, String(SL[i]) + " " + String(SV[i]), PAL.dim);
    bar(x, by + 34, 96, 5, SV[i], MAX_SKILL_PTS, facCol(myFaction.charAt(0)));
  }
  drawHint("ENTER do it  SPACE poke  M msg  <> tabs");
}

static void drawRadar(uint32_t now) {
  // Census strip.
  int cnt[5]; int total = censusCounts(cnt);
  const char CK[5] = { 'B', 'W', 'R', 'G', '?' };
  int x = 6, w = 308;
  for (int i = 0; i < 5; i++) {
    int sw = total ? cnt[i] * w / total : 0;
    if (i == 4) sw = 6 + w - x;
    if (sw > 0) GR.fillRect(x, 33, sw, 5, i == 4 ? PAL.line : facCol(CK[i]));
    x += sw;
  }
  String cs = "";
  for (int i = 0; i < 5; i++) if (cnt[i]) cs += String(CK[i]) + ":" + String(cnt[i]) + " ";
  txt(6, 41, cs, PAL.dim);
  txt(6 + txtW(cs), 41, "(incl. you)", PAL.line);
  txtR(314, 41, String(knownCount) + " in range" + (echoCount ? " +" + String(echoCount) + " echo" : ""), PAL.dim);

  Row rows[1 + MAX_KNOWN_NODES + ECHO_MAX_NODES];
  int n = radarRows(rows, 1 + MAX_KNOWN_NODES + ECHO_MAX_NODES);
  int sel = radarSel(rows, n);
  if (!n) {
    // Nothing yet: an animated sweep so it is obviously listening.
    int cx = 160, cy = 130, R = 60;
    for (int r = 20; r <= R; r += 20) GR.drawCircle(cx, cy, r, PAL.line);
    float a = (now % 3000) / 3000.0f * 6.2832f;
    GR.drawLine(cx, cy, cx + (int)(R * cosf(a)), cy + (int)(R * sinf(a)), PAL.accent);
    for (int k = 1; k < 12; k++) {
      float b = a - k * 0.06f;
      GR.drawLine(cx, cy, cx + (int)(R * cosf(b)), cy + (int)(R * sinf(b)),
                 avMix(PAL.bg, PAL.accent, 200 - k * 16));
    }
    txtC(160, 200, "Listening... nobody in range yet.", PAL.dim);
    drawHint("Devices beacon every ~40 s. Keep walking.");
    return;
  }
  const int ROWH = 25, TOP = 52, VIS = 7;
  int& sc = ui.scroll[T_RADAR];
  if (sel < sc) sc = sel;
  if (sel >= sc + VIS) sc = sel - VIS + 1;
  bool echoDiv = false;
  for (int r = sc; r < n && r < sc + VIS; r++) {
    const Row& row = rows[r];
    int y = TOP + (r - sc) * ROWH;
    bool on = (r == sel);
    if (on) GR.fillRoundRect(2, y - 1, 316, ROWH - 1, 4, PAL.panel), GR.drawRoundRect(2, y - 1, 316, ROWH - 1, 4, PAL.accent);
    String nm = rowName(row);
    if (row.echo) {
      if (!echoDiv) { GR.drawFastHLine(6, y - 2, 308, PAL.line); echoDiv = true; }
      txt(8, y + 2, clip(nm, 22), PAL.dim);
      txt(8, y + 12, "echo, via " + clip(nodeDisplayName(echoNodes[row.echoIdx].via), 14), PAL.dim);
      txtR(314, y + 7, "MAIL", PAL.dim);
      continue;
    }
    int bars = row.training ? 4 : nodeSignalBars(row.n);
    sigBars(8, y + 2, bars, PAL.accent);
    char f = row.training ? (trainScore >= RECON_T_FACTION ? 'G' : '?') : nodeKnownFaction(row.n);
    int lvl = row.training ? (trainScore >= RECON_T_LEVEL ? 1 : 0) : nodeKnownLevel(row.n);
    uint16_t nc = f == '?' ? PAL.fg : facCol(f);
    txt(24, y + 2, clip(nm, 14), nc);
    txt(24 + 15 * 6, y + 2, lvl ? "LV" + String(lvl) : "LV?", PAL.dim);
    const char* prox = row.training ? "SIMULATED" : nodeProximity(row.n);
    String sub = String(prox);
    if (!row.training) sub += "  " + fmtAge(ageMs(row.n->last_seen_ms));
    txt(24, y + 13, sub, PAL.dim);
    // Intel dots.
    int intel = row.training ? trainScore : row.n->intel;
    for (int k = 0; k < RECON_MAX_SEQ; k++) {
      int dx = 150 + k * 7;
      if (k < intel) GR.fillCircle(dx, y + 17, 2, PAL.accent); else GR.drawCircle(dx, y + 17, 2, PAL.line);
    }
    // Status.
    String st; uint16_t sc2 = PAL.fg;
    if (row.training) {
      if (trainScore >= RECON_T_FIREWALL) { int w = breachWindowMs(breachSpecFor(TRAINING_ID).tu); st = breachGrade(w); sc2 = breachGradeCol(w); }
      else { st = "PRACTICE"; sc2 = PAL.warn; }
    }
    else {
      String nid = chipIdStr(row.id);
      unsigned long cd = hackCooldownLeft(nid);
      if (row.n->pwned) { st = "BACKDOOR"; sc2 = PAL.good; }
      if (recentlyHacked(nid)) { st = "OWNED " + fmtLeft(cd); sc2 = PAL.good; }
      else if (cd > 0)         { st = "LOCK " + fmtLeft(cd); sc2 = PAL.bad; }
      else if (!nodeCanHack(row.n)) { st = "IMMUNE"; sc2 = PAL.dim; }
      else if (nodeOdds(row.n) >= 0 && !row.n->pwned) { int w = breachWindowMs(breachSpecFor(row.id).tu); st = breachGrade(w); sc2 = breachGradeCol(w); }
      else if (!st.length()) { st = "SCOUT"; sc2 = PAL.dim; }
      if (row.n->msg_unread) { GR.fillCircle(314, y + 4, 3, PAL.warn); }
    }
    txtR(310, y + 2, st, sc2);
  }
  if (n > VIS) {                                     // scrollbar
    int th = 175 * VIS / n, ty = TOP + 175 * sc / n;
    GR.fillRect(318, ty, 2, th, PAL.line);
  }
  drawHint("^v select  ENTER open  S scout  X hack  M msg");
}

static void drawDossier(uint32_t now) {
  panel(2, 32, 316, 192, PAL.accent);
  bool training = ui.target == TRAINING_ID;
  KnownNode* n = training ? nullptr : findNode(ui.target);
  if (ui.targetEcho) {
    txt(10, 40, echoDisplayName(ui.target), PAL.fg, 2);
    EchoNode* e = findEcho(ui.target);
    txt(10, 64, "Out of your range.", PAL.dim);
    if (e) txt(10, 76, nodeDisplayName(e->via) + " can hear them.", PAL.dim);
    txt(10, 96, "You cannot scout or hack an echo, but you can", PAL.fg);
    txt(10, 108, "send mail: it travels via the one who hears them.", PAL.fg);
    drawHint("M send mail  BKSP back");
    return;
  }
  if (training && !trainingActive()) {
    txtC(160, 110, "Training is over. You are ready.", PAL.dim);
    drawHint("BKSP back");
    return;
  }
  if (!training && !n) {
    txtC(160, 110, "Out of range now.", PAL.dim);
    drawHint("BKSP back");
    return;
  }
  Row row = { ui.target, n, training, false, -1 };
  String nm = rowName(row);
  char f = training ? (trainScore >= RECON_T_FACTION ? 'G' : '?') : nodeKnownFaction(n);
  int lvl = training ? (trainScore >= RECON_T_LEVEL ? 1 : 0) : nodeKnownLevel(n);
  txt(10, 38, clip(nm, 14), f == '?' ? PAL.fg : facCol(f), 2);
  if (f != '?') chip(190, 40, facName(f), facCol(f));
  txtR(310, 42, lvl ? "LV " + String(lvl) : "LV ?", PAL.fg);
  String sub = training ? String("SIMULATED  practice target") :
               String(nodeProximity(n)) + "  " + String(nodeAvgRssi(n)) + " dBm  seen " + fmtAge(ageMs(n->last_seen_ms)) + " ago";
  sigBars(10, 58, training ? 4 : nodeSignalBars(n), PAL.accent);
  txt(26, 60, sub, PAL.dim);

  // Dossier tiers.
  int intel = training ? trainScore : n->intel;
  String val[7];
  val[0] = intel >= RECON_T_NAME ? nm : "";
  val[1] = f != '?' ? String(facName(f)) : "";
  val[2] = lvl ? String(lvl) : "";
  int br = training ? (trainScore >= RECON_T_BRUTE ? TRAIN_BRUTE : -1) : nodeKnownBrute(n);
  int st = training ? (trainScore >= RECON_T_STEALTH ? TRAIN_STEALTH : -1) : nodeKnownStealth(n);
  int fw = training ? (trainScore >= RECON_T_FIREWALL ? TRAIN_FIREWALL : -1) : nodeKnownFirewall(n);
  val[3] = br >= 0 ? String(br) : ""; val[4] = st >= 0 ? String(st) : ""; val[5] = fw >= 0 ? String(fw) : "";
  val[6] = (!training && n->pwned) ? "OPEN" : "";
  for (int i = 0; i < 7; i++) {
    int y = 76 + i * 12;
    bool got = val[i].length() > 0;
    txt(12, y, String(RC_LABEL[i]), got ? PAL.fg : PAL.dim);
    txt(80, y, got ? val[i] : "round " + String(RC_TIERS[i]), got ? PAL.accent : PAL.dim);
  }
  // Right column: recon & odds.
  int rx = 172, y = 76;
  int used = training ? trainRecon : n->recon_count;
  txt(rx, y, "RECON", PAL.fg);
  bool freeRecon = !training && n->pwned;
  for (int k = 0; k < 3; k++) {                  // filled = attempts left
    int cx = rx + 44 + k * 10;
    if (freeRecon || k >= used) GR.fillCircle(cx, y + 3, 3, PAL.accent); else GR.drawCircle(cx, y + 3, 3, PAL.line);
  }
  txt(rx + 78, y, freeRecon ? "free" : String(used < 3 ? 3 - used : 0) + " left", PAL.dim);
  y += 12;
  txt(rx, y, "BEST RUN " + String(training ? trainScore : n->recon_score) + "/10", PAL.dim); y += 12;
  // The breach you would face: how long the ball sits in the gap each pass.
  BreachSpec bs = breachSpecFor(training ? TRAINING_ID : n->chip_id);
  int win = breachWindowMs(bs.tu);
  txt(rx, y, "BREACH", PAL.fg);
  txt(rx + 48, y - 3, breachGrade(win), breachGradeCol(win), 2);
  y += 16;
  txt(rx, y, "gap " + String(bs.tu.zoneW) + "px  " + String(win) + "ms" +
      ((!bs.knowB || !bs.knowS || !bs.knowF) ? " (guess)" : ""), PAL.dim);
  y += 14;
  // Why things are greyed out, in the portal's words.
  String why = "";
  bool canRecon = training ? trainRecon < 3 : nodeCanRecon(n);
  bool canHack = true;
  if (!training) {
    String nid = chipIdStr(n->chip_id);
    unsigned long cd = hackCooldownLeft(nid);
    if (recentlyHacked(nid))      { why = "OWNED. Locked " + fmtLeft(cd) + "."; canHack = false; }
    else if (cd > 0)              { why = "LOCKED OUT " + fmtLeft(cd) + ". Recon too."; canHack = false; }
    else if (!nodeCanHack(n))     { why = "Immune: WHITE can only attack BLACK and RED."; canHack = false; }
    else if (!canRecon)           why = "Recon spent. Hack it, or wait out the cooldown.";
    if (n->pwned && !why.length()) why = "Backdoor open: recon is free.";
    if (intel < RECON_T_NAME && !why.length()) why = "Unidentified. 2 rounds of recon for a name.";
  } else {
    why = "Practice target. Gone once you reach LV 2.";
    if (trainRecon >= 3) why = "Practice recon spent: try the hack.";
  }
  String W[3]; int wn = wrap(why, 23, W, 3);
  for (int i = 0; i < wn; i++) txt(rx, y + i * 10, W[i], PAL.dim);
  // Actions.
  int ay = 168;
  GR.drawFastHLine(8, ay - 4, 304, PAL.line);
  bool armed = ui.hackArmUntil && (int32_t)(ui.hackArmUntil - now) > 0;
  auto btn = [&](int x, const String& k, const String& label, bool enabled, uint16_t c) {
    uint16_t col = enabled ? c : PAL.line;
    GR.drawRoundRect(x, ay, 72, 22, 4, col);
    txt(x + 5, ay + 7, k, col); txt(x + 5 + txtW(k) + 4, ay + 7, label, enabled ? PAL.fg : PAL.line);
  };
  btn(10,  "S", (!training && n->pwned) ? "RE-ENTER" : "SCOUT", canRecon, PAL.accent);
  btn(86,  "X", armed ? "CONFIRM" : "HACK", canHack, armed ? PAL.bad : PAL.warn);
  btn(162, "M", "MESSAGE", !training, PAL.accent);
  btn(238, "P", "PING", !training && !loraActionPending(), PAL.dim);
  if (armed) {
    txtC(160, ay + 30, String(breachGrade(win)) + " breach. X again: stop the ball in the gap.", PAL.bad);
  } else if (skillPoints > 0) txtC(160, ay + 30, "Unspent skill points widen your gap: K", PAL.warn);
  drawHint("S scout  X hack  M msg  P ping  BKSP back");
}

static void drawRecon(uint32_t now) {
  panel(2, 32, 316, 192, PAL.accent);
  bool training = rc.target == TRAINING_ID;
  String nm = training ? (trainScore >= RECON_T_NAME ? String("TRAINING") : String("UNKNOWN-0001"))
                       : nodeDisplayName(rc.target);
  txt(10, 38, "RECON  " + clip(nm, 16), PAL.fg);
  if (rc.len) txtR(310, 38, "ROUND " + String(rc.len) + "  best " + String(rc.best) + " = +" +
                   String(rc.best * 3) + "% gap", PAL.accent);
  else        txtR(310, 38, "10 rounds max", PAL.dim);
  // Grid.
  const int TS = 44, GAP = 5, GX = 14, GY = 52;
  for (int i = 0; i < 9; i++) {
    int x = GX + (i % 3) * (TS + GAP), y = GY + (i / 3) * (TS + GAP);
    bool lit = (rc.flash == i);
    uint16_t base = avMix(PAL.panel, facCol("BWRG"[i % 4]), 60);
    uint16_t c = lit ? (rc.flashBad ? PAL.bad : PAL.accent) : base;
    GR.fillRoundRect(x, y, TS, TS, 6, c);
    GR.drawRoundRect(x, y, TS, TS, 6, lit ? PAL.fg : PAL.line);
    if (rc.phase == RC_INPUT && rc.cursor == i) GR.drawRoundRect(x - 2, y - 2, TS + 4, TS + 4, 7, PAL.fg);
    txt(x + TS / 2 - 6, y + TS / 2 - 8, String((char)toupper(RC_KEYS[i])), lit ? PAL.bg : PAL.fg, 2);
    txt(x + TS - 9, y + TS - 11, String(i + 1), lit ? PAL.bg : PAL.dim);
  }
  // Dossier column fills in as tiers land.
  int dx = 182;
  for (int i = 0; i < 7; i++) {
    int y = 54 + i * 20;
    bool got = rc.field[i].length() > 0;
    txt(dx, y, RC_LABEL[i], got ? PAL.fg : PAL.dim);
    txt(dx, y + 9, got ? clip(rc.field[i], 20) : "round " + String(RC_TIERS[i]), got ? PAL.accent : PAL.dim);
  }
  // Status line.
  String s; uint16_t sc = PAL.fg;
  switch (rc.phase) {
    case RC_LINK:   { const char* sp = "|/-\\"; int left = (int32_t)(rc.deadline - now) > 0 ? (rc.deadline - now) / 1000 : 0;
                      s = String("Establishing link ") + sp[(now / 150) & 3] + "  " + String(left) + "s"; } break;
    case RC_READY:  s = "Link up. Watch, then repeat. ENTER to start."; sc = PAL.accent; break;
    case RC_WATCH:  s = "Watch..."; break;
    case RC_INPUT:  s = "Your turn: " + String(rc.pos) + "/" + String(rc.len); sc = PAL.accent; break;
    case RC_BAD:    s = "Wrong tile."; sc = PAL.bad; break;
    case RC_NEXT:   s = "Correct. Next round."; sc = PAL.good; break;
    case RC_BACKDOOR: s = "Backdoor still open. Pulling their file..."; sc = PAL.good; break;
    case RC_DONE: case RC_NOLINK: s = rc.msg; sc = rc.phase == RC_NOLINK ? PAL.bad : PAL.accent; break;
  }
  String L[2]; int n = wrap(s, 50, L, 2);
  for (int i = 0; i < n; i++) txt(10, 202 + i * 10, L[i], sc);
  if (rc.phase == RC_INPUT) drawHint("WER/SDF/ZXC or 1-9 or ball+press  BKSP end");
  else if (rc.phase == RC_DONE || rc.phase == RC_NOLINK) drawHint("ENTER / BKSP close");
  else if (rc.phase == RC_READY) drawHint("ENTER start  BKSP back");
  else if (rc.phase == RC_LINK) drawHint("BKSP give up");
  else drawHint("BKSP end run");
}

static void drawCompose() {
  panel(2, 32, 316, 192, PAL.accent);
  txt(10, 40, "TO", PAL.dim);
  if (cm.to) {
    bool direct = findNode(cm.to) != nullptr;
    String nm = direct ? nodeDisplayName(cm.to) : echoDisplayName(cm.to);
    txt(30, 38, "< " + clip(nm, 16) + " >", PAL.fg, 2);
    txt(30, 58, direct ? "in range: sent now" : "out of range: travels as mail", PAL.dim);
  } else txt(30, 40, "nobody in range yet", PAL.bad);
  GR.drawRoundRect(8, 76, 304, 60, 4, PAL.line);
  String t = cm.text + (((millis() / 500) & 1) ? "_" : " ");
  const int PER = 25;
  for (int i = 0; i < 3 && i * PER < (int)t.length(); i++)
    txt(14, 82 + i * 18, t.substring(i * PER, min((int)t.length(), (i + 1) * PER)), PAL.fg, 2);
  txtR(310, 140, String(cm.text.length()) + "/" + String(MAIL_TEXT_MAX), cm.text.length() >= MAIL_TEXT_MAX ? PAL.warn : PAL.dim);
  // Last few in the conversation with this person.
  int y = 156;
  for (int i = 0; i < (int)msgLogCount && y < 214; i++) {
    const MsgLogEntry* e = msgLogAt(i);
    if (!e || e->from != cm.to) continue;
    txt(10, y, clip(String(e->text), 50), PAL.dim); y += 10;
  }
  drawHint("^v/<> who  ENTER send  BKSP erase / close");
}

static void drawInbox(uint32_t now) {
  String bag = "Outbox " + String(mailPending()) + "  carrying " + String(mailCarriedCount()) +
               "  delivered " + String(loraMailDelivered);
  txt(6, 34, bag, PAL.dim);
  int n = msgLogCount;
  if (!n) {
    txtC(160, 110, "No messages yet.", PAL.dim);
    txtC(160, 124, "Press M to write one: 32 characters, over the air.", PAL.dim);
  }
  int& sel = ui.sel[T_INBOX];
  if (sel >= n) sel = n ? n - 1 : 0;
  const int ROWH = 36, TOP = 46, VIS = 4;
  int& sc = ui.scroll[T_INBOX];
  if (sel < sc) sc = sel;
  if (sel >= sc + VIS) sc = sel - VIS + 1;
  for (int i = sc; i < n && i < sc + VIS; i++) {
    const MsgLogEntry* e = msgLogAt(i);
    if (!e) break;
    int y = TOP + (i - sc) * ROWH;
    if (i == sel) GR.fillRoundRect(2, y - 1, 316, ROWH - 2, 4, PAL.panel), GR.drawRoundRect(2, y - 1, 316, ROWH - 2, 4, PAL.accent);
    String from = nodeDisplayName(e->from);
    txt(8, y + 2, clip(from, 20), PAL.fg);
    if (e->via) txt(8 + txtW(clip(from, 20)) + 6, y + 2, "via " + clip(nodeDisplayName(e->via), 12), PAL.warn);
    txtR(312, y + 2, fmtAge(ageMs(e->at)) + " ago", PAL.dim);
    String L[2]; int wn = wrap(String(e->text), 50, L, 2);
    for (int k = 0; k < wn; k++) txt(8, y + 13 + k * 10, L[k], PAL.accent);
  }
  if (lastSentAt) {
    GR.drawFastHLine(6, 196, 308, PAL.line);
    txt(6, 200, clip("You to " + nodeDisplayName(lastSentTo) + ": " + lastSentText, 44), PAL.dim);
    txtR(314, 200, fmtAge(ageMs(lastSentAt)), PAL.dim);
  }
  drawHint(n ? "^v select  ENTER reply  M new message" : "M new message");
}

static const char* SKILL_KEY[3] = { "brute", "stealth", "firewall" };
static const char* SKILL_TXT[3] = {
  "Widens your gap in a breach. Hacked: blunts their STEALTH.",
  "Hacked: shrinks their gap. Breaching: counters their BRUTE.",
  "Hacked: their ball runs faster. Losses cost 15-2*FW XP.",
};
static void drawSkills() {
  txt(6, 36, "SKILL POINTS", PAL.fg);
  txt(90, 34, String(skillPoints), skillPoints ? PAL.warn : PAL.dim, 2);
  txtR(314, 36, "one per level", PAL.dim);
  int SV[3] = { skillBrute, skillStealth, skillFirewall };
  int& sel = ui.sel[T_SKILLS];
  if (sel > 2) sel = 2;
  for (int i = 0; i < 3; i++) {
    int y = 56 + i * 54;
    bool on = sel == i;
    panel(4, y, 312, 50, on ? PAL.accent : PAL.line);
    String nm = String(SKILL_KEY[i]); nm.toUpperCase();
    txt(12, y + 6, nm, PAL.fg, 2);
    txtR(308, y + 6, String(SV[i]), facCol(myFaction.charAt(0)), 2);
    bar(124, y + 10, 140, 8, SV[i], MAX_SKILL_PTS, facCol(myFaction.charAt(0)));
    String L[2]; int n = wrap(SKILL_TXT[i], 49, L, 2);
    for (int k = 0; k < n; k++) txt(12, y + 26 + k * 10, L[k], PAL.dim);
  }
  drawHint(skillPoints ? "^v choose  ENTER spend 1 point" : "Level up to earn points. Win hacks for XP.");
}

static void drawLog() {
  int total = statWon + statLost;
  const char* K[6] = { "WON", "LOST", "HELD", "BREACHED", "MET", "BEST RUN" };
  int V[6] = { statWon, statLost, statHeld, statBreached, statMet, statBestSeq };
  for (int i = 0; i < 6; i++) {
    int x = 6 + (i % 3) * 104, y = 34 + (i / 3) * 24;
    txt(x, y, K[i], PAL.dim);
    txt(x, y + 9, String(V[i]) + (i == 5 ? "/10" : ""), PAL.fg, 1);
  }
  if (total) txtR(314, 34, String(statWon * 100 / total) + "% win", PAL.accent);
  GR.drawFastHLine(6, 82, 308, PAL.line);
  int& sc = ui.scroll[T_LOG];
  const int VIS = 13;
  if (sc > eventCount - VIS) sc = eventCount - VIS;
  if (sc < 0) sc = 0;
  if (!eventCount) txtC(160, 130, "Nothing has happened yet.", PAL.dim);
  for (int k = sc; k < eventCount && k < sc + VIS; k++) {
    int idx = (eventNext - 1 - k + EVENT_LOG_SIZE * 2) % EVENT_LOG_SIZE;
    GameEvent& e = eventLog[idx];
    int y = 86 + (k - sc) * 10;
    String who = e.peer ? nodeDisplayName(e.peer) : String("");
    txt(6, y, clip(String(evName(e.type)) + " " + who, 34), PAL.fg);
    if (e.type == EV_RECON) txtR(256, y, "seq " + String(e.xp), PAL.accent);
    else if (e.xp) txtR(256, y, (e.xp > 0 ? "+" : "") + String(e.xp) + " XP", e.xp > 0 ? PAL.good : PAL.bad);
    txtR(314, y, fmtAge(ageMs(e.at)) + " ago", PAL.dim);
  }
  drawHint("^v scroll");
}

// Options: label, value.
enum { O_THEME, O_SOUND, O_BRIGHT, O_TIMEOUT, O_KBL, O_BEACON, O_DIAG, O_PORTAL, O_PASSWORD, O_CLEAR, O_ABOUT, O_RESET, O_COUNT };
static String optValue(int i) {
  switch (i) {
    case O_THEME:   return PAL.name;
    case O_SOUND:   return VOL_TXT[tdp.volume];
    case O_BRIGHT:  return String(tdp.bright) + "/16";
    case O_TIMEOUT: return TIMEOUT_TXT[tdp.timeout];
    case O_KBL:     return KB_TXT[tdp.kblight];
    case O_PORTAL:  return tdp.portal ? "ON" : "OFF";
    case O_PASSWORD:return tdp.portal ? myPassword : String("(portal off)");
    default:        return ">";
  }
}
static const char* OPT_NAME[O_COUNT] = { "Theme", "Sound", "Brightness", "Screen off after", "Keyboard light",
  "Send beacon now", "Radio diagnostics", "Phone portal (Wi-Fi)", "Portal password",
  "Forget nodes in range", "About this device", "Factory reset" };
static void drawOptions() {
  int& sel = ui.sel[T_OPTS];
  if (sel >= O_COUNT) sel = O_COUNT - 1;
  const int ROWH = 16, TOP = 34, VIS = 12;
  int& sc = ui.scroll[T_OPTS];
  if (sel < sc) sc = sel;
  if (sel >= sc + VIS) sc = sel - VIS + 1;
  for (int i = sc; i < O_COUNT && i < sc + VIS; i++) {
    int y = TOP + (i - sc) * ROWH;
    bool on = sel == i;
    if (on) GR.fillRoundRect(2, y - 2, 316, ROWH - 1, 3, PAL.panel), GR.drawRoundRect(2, y - 2, 316, ROWH - 1, 3, PAL.accent);
    txt(10, y + 2, OPT_NAME[i], i == O_RESET ? PAL.bad : PAL.fg);
    txtR(310, y + 2, clip(optValue(i), 20), on ? PAL.accent : PAL.dim);
  }
  if (tdp.portal) txt(6, 212, "Portal: join Wi-Fi " + clip(WiFi.softAPSSID(), 18) + ", open 192.168.4.1", PAL.dim);
  drawHint("^v choose  ENTER change");
}

static void drawDiag() {
  panel(2, 32, 316, 192, PAL.accent);
  txt(10, 38, "RADIO", PAL.fg, 2);
  int y = 60;
  auto kv = [&](const String& k, const String& v) { txt(12, y, k, PAL.dim); txt(130, y, v, PAL.fg); y += 11; };
  kv("status", loraStatus);
  kv("profile", String(PROFILE_NAME) + "  " + String(LORA_FREQ, 3) + " MHz  SF" + String(LORA_SF));
  kv("packets", String(loraPktSent) + " sent  " + String(loraPktRecv) + " received");
  kv("last signal", String(loraLastRSSI) + " dBm  SNR " + String(loraLastSNR, 1));
  kv("airtime", String(dutyCyclePct(), 2) + "% of " + String(LORA_DUTY_SELF_PCT, 1) + "% budget");
  kv("in range", String(knownCount) + "  echoes " + String(echoCount));
  kv("mail", String(mailPending()) + " out  " + String(mailCarriedCount()) + " carried  " + String(loraMailDelivered) + " delivered");
  kv("free memory", String(ESP.getFreeHeap() / 1024) + " KB");
  kv("uptime", fmtAge(millis()));
  kv("firmware", String(FIRMWARE_VERSION) + " T-Deck");
  drawHint("BKSP back");
}

static void drawAbout() {
  panel(2, 32, 316, 192, PAL.accent);
  txt(10, 38, myName, facCol(myFaction.charAt(0)), 2);
  int y = 62;
  auto kv = [&](const String& k, const String& v) { txt(12, y, k, PAL.dim); txt(110, y, v, PAL.fg); y += 12; };
  kv("faction", myFaction);
  kv("level", String(myLevel) + "  (" + String(myXP) + "/" + String(xpForNextLevel()) + " XP)");
  kv("chip id", chipIdStr(myChipID32));
  kv("radio", String(PROFILE_NAME) + " profile");
  kv("battery", String(battV, 2) + " V");
  y += 6;
  txt(12, y, "Your codename comes from your chip, so it is", PAL.dim); y += 10;
  txt(12, y, "the same on every device that hears you.", PAL.dim); y += 16;
  txt(12, y, "Cypher32 T-Deck. No phone needed.", PAL.accent);
  drawHint("BKSP back");
}

static void drawConfirm() {
  panel(20, 80, 280, 80, PAL.warn);
  String L[3]; int n = wrap(ui.confirmMsg, 42, L, 3);
  for (int i = 0; i < n; i++) txtC(160, 92 + i * 12, L[i], PAL.fg);
  txtC(160, 142, "ENTER yes    BKSP no", PAL.warn);
  drawHint("ENTER confirm  BKSP cancel");
}

static void drawText() {
  panel(20, 70, 280, 100, PAL.accent);
  String L[2]; int n = wrap(ui.textPrompt, 42, L, 2);
  for (int i = 0; i < n; i++) txtC(160, 80 + i * 11, L[i], PAL.fg);
  GR.drawRoundRect(34, 110, 252, 24, 4, PAL.line);
  txt(40, 114, ui.textBuf + (((millis() / 500) & 1) ? "_" : " "), PAL.accent, 2);
  drawHint("type  ENTER ok  BKSP erase / cancel");
}

// Result cards share HOME's layout: the avatar on the left, reacting.
static void drawCard(uint32_t now) {
  GR.fillRect(0, 30, TDECK_W, 196, PAL.bg);
  blitAvatar(AV_X, AV_Y);
  uint16_t cc = toneCol(ui.cardTone);
  panel(148, 38, 168, 150, cc);
  txt(156, 46, clip(ui.cardTitle, 13), cc, 2);
  int y = 70;
  for (int i = 0; i < 4; i++) {
    if (!ui.cardLine[i].length()) continue;
    String L[4]; int n = wrap(ui.cardLine[i], 25, L, 4);
    for (int k = 0; k < n && y < 180; k++) { txt(156, y, L[k], i == 0 ? PAL.fg : PAL.dim); y += 11; }
    y += 4;
  }
  if (ui.cardKind == CARD_ARMED) drawHint("Hold the trackball 5 s to wipe. Or wait.");
  else if (ui.cardKind == CARD_WIPING) drawHint("Keep holding to wipe. Let go to cancel.");
  else if (ui.cardKind == CARD_MSG) drawHint("ENTER reply  BKSP close");
  else drawHint("any key to continue");
}

static void drawBreach(uint32_t now) {
  GR.fillRect(0, 30, TDECK_W, 196, PAL.bg);
  blitAvatar(AV_X, AV_Y);
  panel(148, 38, 168, 150, PAL.warn);
  txt(156, 46, "BREACHING", PAL.warn, 2);
  txt(156, 70, clip(ui.breachName, 24), PAL.fg);
  // Scrolling hex, because it is fun.
  for (int k = 0; k < 6; k++) {
    uint32_t v = (now / 90 + k * 7919) * 2654435761u;
    char b[24]; snprintf(b, sizeof b, "%08lX %04X", (unsigned long)v, (unsigned)(v >> 7) & 0xFFFF);
    txt(156, 88 + k * 11, b, avMix(PAL.bg, PAL.accent, 90 + k * 25));
  }
  txt(156, 160, "Waiting for their", PAL.dim);
  txt(156, 171, "firewall to answer...", PAL.dim);
  drawHint("");
}

// Setup: welcome, faction, confirm.
static const char* FAC[4] = { "BLACK", "WHITE", "RED", "GREEN" };
static const char* FAC_TXT[4] = {
  "+3 BRUTE. Hits hardest: +20% XP on every win. A loss always costs the full 15 XP.",
  "+3 FIREWALL. The defender. May only attack BLACK and RED.",
  "+3 STEALTH. +25% XP against GREEN, but 15% of wins get traced and cost you XP.",
  "+1 to everything. +10% XP against BLACK, but WHITE bites back 25% of the time.",
};
static void drawSetup(uint32_t now) {
  GR.fillRect(0, 16, TDECK_W, 210, PAL.bg);
  if (ui.setupStep == 0) {
    blitAvatar(AV_X, AV_Y);
    txt(152, 40, "CYPHER32", PAL.accent, 2);
    String L[10]; int n = wrap("You are a ghost in the machine. Carry this device. When other players come "
                               "within range it finds them by radio. Scout them, breach them, level up. "
                               "No phone, no internet, no names sent.", 27, L, 10);
    for (int i = 0; i < n; i++) txt(152, 64 + i * 11, L[i], PAL.fg);
    txt(152, 64 + n * 11 + 8, "Your codename: " + nodeNameFromId(myChipID32), PAL.accent);
    drawHint("ENTER begin");
  } else if (ui.setupStep == 1) {
    txt(8, 22, "CHOOSE YOUR FACTION", PAL.fg, 2);
    txt(8, 40, "Permanent until a factory reset.", PAL.dim);
    for (int i = 0; i < 4; i++) {
      int y = 54 + i * 42;
      bool on = ui.setupFac == i;
      uint16_t c = facCol(FAC[i][0]);
      panel(4, y, 312, 38, on ? c : PAL.line);
      txt(12, y + 5, FAC[i], c, 2);
      String L[3]; int n = wrap(FAC_TXT[i], 36, L, 3);
      for (int k = 0; k < n; k++) txt(92, y + 4 + k * 10, L[k], on ? PAL.fg : PAL.dim);
    }
    drawHint("^v choose  ENTER pick  BKSP back");
  } else {
    blitAvatar(AV_X, AV_Y);
    txt(152, 40, "READY?", PAL.accent, 2);
    txt(152, 66, "Codename", PAL.dim);
    txt(152, 76, nodeNameFromId(myChipID32), PAL.fg, 2);
    txt(152, 100, "Faction", PAL.dim);
    txt(152, 110, FAC[ui.setupFac], facCol(FAC[ui.setupFac][0]), 2);
    txt(152, 136, "Press ENTER to jack in.", PAL.fg);
    txt(152, 148, "The device restarts once.", PAL.dim);
    drawHint("ENTER jack in  BKSP back");
  }
}

#include "tdeck_breach.h"

// ── one frame ───────────────────────────────
static void render(uint32_t now) {
  GR.fillScreen(PAL.bg);
  drawStatus();
  if (ui.modal == M_SETUP) { drawSetup(now); drawToast(); return; }
  drawTabs();
  switch (ui.modal) {
    case M_NONE:
      switch (ui.tab) {
        case T_HOME:   drawHome(now); break;
        case T_RADAR:  drawRadar(now); break;
        case T_INBOX:  drawInbox(now); break;
        case T_SKILLS: drawSkills(); break;
        case T_LOG:    drawLog(); break;
        case T_OPTS:   drawOptions(); break;
      }
      break;
    case M_DOSSIER: drawDossier(now); break;
    case M_RECON:   drawRecon(now); break;
    case M_COMPOSE: drawCompose(); break;
    case M_CONFIRM: drawConfirm(); break;
    case M_TEXT:    drawText(); break;
    case M_CARD:    drawCard(now); break;
    case M_DIAG:    drawDiag(); break;
    case M_ABOUT:   drawAbout(); break;
    case M_BREACH:  drawBreach(now); break;
    case M_BREACHGAME: drawBreachGame(now); break;
  }
  drawToast();
}

// Is the avatar on screen right now, and where?
static bool avatarVisible() {
  if (ui.modal == M_CARD || ui.modal == M_BREACH) return true;
  if (ui.modal == M_SETUP) return ui.setupStep != 1;
  return ui.modal == M_NONE && ui.tab == T_HOME;
}

// ─────────────────────────────────────────────
//  Input
// ─────────────────────────────────────────────
static void gotoTab(int t) {
  ui.tab = (uint8_t)((t + T_COUNT) % T_COUNT);
  ui.modal = M_NONE; ui.target = 0; ui.dirty = true;
  sfx(SFX_NAV);
}
static void openDossier(uint32_t id, bool echo) {
  ui.target = id; ui.targetEcho = echo; ui.hackArmUntil = 0;
  if (!echo) clearUnread(id);
  ui.modal = M_DOSSIER; ui.dirty = true; sfx(SFX_CLICK);
}
static void confirm(uint8_t what, const String& msg) {
  ui.confirmWhat = what; ui.confirmMsg = msg; ui.modal = M_CONFIRM; ui.dirty = true;
}
static void askText(uint8_t what, const String& prompt) {
  ui.textWhat = what; ui.textPrompt = prompt; ui.textBuf = ""; ui.modal = M_TEXT; ui.dirty = true;
}
static void closeModal() {
  ui.modal = (ui.target && ui.modal != M_DOSSIER) ? M_DOSSIER : M_NONE;
  if (ui.modal == M_NONE) ui.target = 0;
  ui.dirty = true; sfx(SFX_BACK);
}

static void optionChange(int i) {
  switch (i) {
    case O_THEME:   tdp.theme = (tdp.theme + 1) % 4; break;
    case O_SOUND:   tdp.volume = (tdp.volume + 1) % 4; sndVolume = tdp.volume; break;
    case O_BRIGHT:  tdp.bright = tdp.bright >= 16 ? 2 : tdp.bright + 2; tdeckBacklight(tdp.bright); break;
    case O_TIMEOUT: tdp.timeout = (tdp.timeout + 1) % 5; break;
    case O_KBL:     tdp.kblight = (tdp.kblight + 1) % 3; tdeckKbBacklight(KB_DUTY[tdp.kblight]); break;
    case O_BEACON:  if (!loraReady) { toast("Radio offline", TT_BAD); return; }
                    loraSendBeacon(); toast("Beacon sent", TT_GOOD); sfx(SFX_SEND); return;
    case O_DIAG:    ui.modal = M_DIAG; ui.dirty = true; return;
    case O_ABOUT:   ui.modal = M_ABOUT; ui.dirty = true; return;
    case O_PORTAL:  confirm(CONF_PORTAL, tdp.portal ? "Turn the phone portal OFF? The device restarts."
                                                    : "Turn the phone portal ON? It opens a Wi-Fi network (uses more battery). The device restarts.");
                    return;
    case O_PASSWORD:if (!tdp.portal) { toast("Only used by the phone portal", TT_DIM); return; }
                    askText(TXT_PASSWORD, "New portal password (at least 6 characters)"); return;
    case O_CLEAR:   confirm(CONF_CLEAR, "Forget every node in range? They come back when they next beacon."); return;
    case O_RESET:   askText(TXT_WIPE, "Erase your character for good? Type WIPE and press ENTER."); return;
  }
  tdeckSavePrefs(); sfx(SFX_CLICK); ui.dirty = true;
}

static void doConfirm() {
  uint8_t w = ui.confirmWhat;
  ui.modal = M_NONE; ui.dirty = true;
  if (w == CONF_CLEAR) { actClearNodes(); toast("Node list cleared", TT_GOOD); }
  else if (w == CONF_PORTAL) {
    if (!tdp.portal) {
      // The portal's Wi-Fi is open to anyone in range; the password is all
      // that stands between them and your character. Never open it on the
      // public default, so ask for one first.
      askText(TXT_PORTAL, "Set a portal password (at least 6 characters). Phones will need it.");
      return;
    }
    tdp.portal = 0; tdeckSavePrefs();
    toast("Restarting...", TT_WARN); requestRestart(600);
  }
}

static void doText() {
  if (ui.textWhat == TXT_PASSWORD) {
    if (ui.textBuf.length() < 6) { toast("Password too short", TT_BAD); sfx(SFX_ERR); return; }
    myPassword = ui.textBuf; saveProgress();
    toast("Password updated", TT_GOOD);
    ui.modal = M_NONE; ui.dirty = true;
  } else if (ui.textWhat == TXT_PORTAL) {
    if (ui.textBuf.length() < 6) { toast("Password too short", TT_BAD); sfx(SFX_ERR); return; }
    myPassword = ui.textBuf; saveProgress();
    tdp.portal = 1; tdeckSavePrefs();
    ui.modal = M_NONE; ui.dirty = true;
    toast("Portal on. Restarting...", TT_WARN); requestRestart(800);
  } else if (ui.textWhat == TXT_WIPE) {
    String t = ui.textBuf; t.toUpperCase();
    if (t != "WIPE") { toast("Not wiped", TT_DIM); ui.modal = M_NONE; ui.dirty = true; return; }
    openCard(CARD_WIPING, "WIPING", TT_BAD, "Erasing your character.", "Restarting...", "", "", 0);
    render(millis()); pushAll();
    wipeAndReboot();
  }
}

// Dossier / radar actions on a node.
static void nodeAction(char k, uint32_t id, bool echo) {
  if (echo) {
    if (k == 'm') composeOpen(id);
    else { toast("An echo can only be mailed", TT_DIM); sfx(SFX_ERR); }
    return;
  }
  switch (k) {
    case 's': reconStart(id); break;
    case 'x': {
      uint32_t now = millis();
      if (ui.modal == M_DOSSIER && ui.hackArmUntil && (int32_t)(ui.hackArmUntil - now) > 0) {
        ui.hackArmUntil = 0;
        ActResult ok = actHackCheck(id);
        if (ok.code != 200) { toast(ok.msg, TT_BAD); sfx(SFX_ERR); break; }
        breachOpen(id);
      }
      else { if (ui.modal != M_DOSSIER) openDossier(id, false); ui.hackArmUntil = now + 4000; ui.dirty = true; sfx(SFX_CLICK); }
      break;
    }
    case 'm': if (id == TRAINING_ID) { toast("The dummy does not read mail", TT_DIM); break; } composeOpen(id); break;
    case 'p':
      if (id == TRAINING_ID) break;
      if (!loraReady || loraActionPending()) { toast("Radio busy", TT_BAD); break; }
      loraSendPing(id); toast("Ping sent", TT_DIM); sfx(SFX_SEND); break;
  }
}

// Poke the avatar. It has opinions about that.
static void pokeAvatar() {
  static const char* POKE[] = { "Hey!", "I'm working here.", "...what?", "Stop that.", "Ha. Hi.",
                                "Need something?", "Careful, I bite.", "Scanning. Chill." };
  static const AvReaction PR[] = { AV_R_ALERT, AV_R_NEWNODE, AV_R_WIN };
  av.trigger(PR[avRange(0, 2)], millis(), 1400);
  say(POKE[avRange(0, 7)]); sndPlay(1400, 20); sndPlay(1800, 20);
}

static void onSelect() {
  uint32_t now = millis();
  switch (ui.modal) {
    case M_SETUP:
      if (ui.setupStep < 2) { ui.setupStep++; ui.dirty = true; sfx(SFX_CLICK); }
      else {
        ActResult r = actSetup(FAC[ui.setupFac], "", /*requirePassword=*/false);
        if (r.code != 200) { toast(r.msg, TT_BAD); sfx(SFX_ERR); return; }
        sfx(SFX_LEVELUP);
        ui.modal = M_NONE;                       // setup is done: show this now, not queued
        openCard(CARD_INFO, "JACKED IN", TT_ACCENT, "Welcome, " + myName + ".", "Restarting...", "", "", 0);
        requestRestart(1500);
      }
      return;
    case M_CARD:
      if (ui.cardKind == CARD_ARMED || ui.cardKind == CARD_WIPING) return;
      if (ui.cardKind == CARD_MSG && ui.cardReply) { uint32_t r = ui.cardReply; dismissCard(); composeOpen(r); return; }
      dismissCard(); return;
    case M_CONFIRM: doConfirm(); return;
    case M_TEXT:    doText(); return;
    case M_COMPOSE: composeSend(); return;
    case M_RECON:
      if (rc.phase == RC_READY) { rc.best = 0; rc.len = 0; reconNextRound(); sfx(SFX_CLICK); }
      else if (rc.phase == RC_INPUT) reconTap(rc.cursor);
      else if (rc.phase == RC_DONE || rc.phase == RC_NOLINK) reconClose();
      return;
    case M_DOSSIER: nodeAction('s', ui.target, ui.targetEcho); return;
    case M_DIAG: case M_ABOUT: closeModal(); return;
    case M_BREACH: return;
  }
  switch (ui.tab) {
    case T_HOME: {
      NextMove m = nextMove();
      switch (m.kind) {
        case NX_SKILLS:  gotoTab(T_SKILLS); return;
        case NX_COMPOSE: composeOpen(0); return;
        case NX_DOSSIER:
          ui.tab = T_RADAR; ui.selId = m.id; ui.selEcho = false;
          openDossier(m.id, false);
          return;
        default: pokeAvatar(); return;           // nothing to do: poke instead
      }
    }
    case T_RADAR: {
      Row rows[1 + MAX_KNOWN_NODES + ECHO_MAX_NODES];
      int n = radarRows(rows, 1 + MAX_KNOWN_NODES + ECHO_MAX_NODES);
      if (n) { int s = radarSel(rows, n); openDossier(rows[s].id, rows[s].echo); }
      break;
    }
    case T_INBOX: {
      const MsgLogEntry* e = msgLogAt(ui.sel[T_INBOX]);
      composeOpen(e ? e->from : 0);
      break;
    }
    case T_SKILLS: {
      if (skillPoints < 1) { toast("No skill points yet: level up", TT_DIM); sfx(SFX_ERR); break; }
      ActResult r = actSkill(SKILL_KEY[ui.sel[T_SKILLS]]);
      toast(r.msg, r.code == 200 ? TT_GOOD : TT_BAD);
      if (r.code == 200) { sfx(SFX_ROUND); av.trigger(AV_R_WIN, now, 1500); }
      ui.dirty = true;
      break;
    }
    case T_OPTS: optionChange(ui.sel[T_OPTS]); break;
  }
}

static void onBack() {
  switch (ui.modal) {
    case M_SETUP: if (ui.setupStep) { ui.setupStep--; ui.dirty = true; sfx(SFX_BACK); } return;
    case M_RECON:
      if (rc.phase == RC_DONE || rc.phase == RC_NOLINK || rc.phase == RC_LINK || rc.phase == RC_READY) reconClose();
      else if (rc.phase == RC_BACKDOOR) { rc.best = RECON_MAX_SEQ; reconFinish("Backdoor still open."); }
      else reconFinish("Run ended.");
      return;
    case M_COMPOSE:
      if (cm.text.length()) { cm.text.remove(cm.text.length() - 1); ui.dirty = true; }
      else closeModal();
      return;
    case M_TEXT:
      if (ui.textBuf.length()) { ui.textBuf.remove(ui.textBuf.length() - 1); ui.dirty = true; }
      else closeModal();
      return;
    case M_CARD:
      if (ui.cardKind == CARD_ARMED || ui.cardKind == CARD_WIPING) return;
      dismissCard(); return;
    case M_BREACH: return;
    case M_NONE: if (ui.tab != T_HOME) gotoTab(T_HOME); return;
    default: closeModal(); return;
  }
}

static void onNav(int dx, int dy) {
  // Its eyes follow the ball.
  av.lookTX = dx * 3; av.lookTY = dy * 2; av.lookNext = millis() + 900;
  switch (ui.modal) {
    case M_SETUP:
      if (ui.setupStep == 1 && dy) { ui.setupFac = (uint8_t)((ui.setupFac + 4 + dy) % 4); ui.dirty = true; sfx(SFX_NAV); }
      return;
    case M_RECON:
      if (rc.phase == RC_INPUT) {
        int c = rc.cursor, x = c % 3 + dx, y = c / 3 + dy;
        if (x >= 0 && x < 3 && y >= 0 && y < 3) { rc.cursor = y * 3 + x; ui.dirty = true; }
      }
      return;
    case M_COMPOSE: composeStep(dx ? dx : dy); return;
    case M_BREACH: case M_CONFIRM: case M_TEXT: case M_BREACHGAME: return;
    case M_CARD: if (ui.cardKind == CARD_ARMED || ui.cardKind == CARD_WIPING) return;
                 dismissCard(); return;              // a roll only puts the card away
    case M_DOSSIER: case M_DIAG: case M_ABOUT:
      if (dx) { ui.modal = M_NONE; ui.target = 0; }
      else return;
      break;
  }
  if (dx) { gotoTab(ui.tab + dx); return; }
  int max = 0;
  switch (ui.tab) {
    case T_RADAR: {
      Row rows[1 + MAX_KNOWN_NODES + ECHO_MAX_NODES];
      int n = radarRows(rows, 1 + MAX_KNOWN_NODES + ECHO_MAX_NODES);
      int s = radarSel(rows, n) + dy;
      if (s < 0) s = 0;
      if (s >= n) s = n ? n - 1 : 0;
      ui.sel[T_RADAR] = s;
      if (n) { ui.selId = rows[s].id; ui.selEcho = rows[s].echo; }
      ui.dirty = true; sfx(SFX_NAV);
      return;
    }
    case T_INBOX:  max = msgLogCount; break;
    case T_SKILLS: max = 3; break;
    case T_OPTS:   max = O_COUNT; break;
    case T_LOG:    ui.scroll[T_LOG] += dy; ui.dirty = true; return;
    default: return;
  }
  int& s = ui.sel[ui.tab];
  s += dy;
  if (ui.tab == T_OPTS && max) s = (s + max) % max;       // a short menu wraps
  if (s < 0) s = 0;
  if (s >= max) s = max ? max - 1 : 0;
  ui.dirty = true; sfx(SFX_NAV);
}

static void onKey(char k) {
  // Text entry swallows everything printable.
  if (ui.modal == M_COMPOSE || ui.modal == M_TEXT) {
    if (k == '\r' || k == '\n') { onSelect(); return; }
    if (k == 0x08 || k == 0x7F)  { onBack(); return; }
    String& b = (ui.modal == M_COMPOSE) ? cm.text : ui.textBuf;
    int max = (ui.modal == M_COMPOSE) ? MAIL_TEXT_MAX : 20;
    if (k >= 32 && k <= 126 && (int)b.length() < max) { b += k; ui.dirty = true; sfx(SFX_NAV); }
    return;
  }
  if (ui.modal == M_BREACHGAME) { breachKey(k); return; }   // every key, straight in
  if (k == '\r' || k == '\n') { onSelect(); return; }
  if (k == 0x08 || k == 0x7F) { onBack(); return; }
  char c = (k >= 'A' && k <= 'Z') ? (char)(k - 'A' + 'a') : k;

  if (ui.modal == M_RECON) {
    if (rc.phase == RC_INPUT) {
      for (int i = 0; i < 9; i++) if (c == RC_KEYS[i]) { reconTap(i); return; }
      if (c >= '1' && c <= '9') { reconTap(c - '1'); return; }
    }
    // Space starts a run, but never taps a tile: the T-Deck's space bar is
    // huge, and a stray press would end the run.
    if (c == ' ' && rc.phase != RC_INPUT) onSelect();
    return;
  }
  if (ui.modal == M_SETUP) { if (c == ' ') onSelect(); return; }
  if (ui.modal == M_CONFIRM) { if (c == 'y') onSelect(); else if (c == 'n') onBack(); return; }
  if (ui.modal == M_CARD) {
    // A key only puts the card away. Letting it through as a hotkey meant a
    // card arriving mid-sentence turned the next letter into a tab jump.
    if (ui.cardKind == CARD_ARMED || ui.cardKind == CARD_WIPING) return;
    if (c == 'r' && ui.cardKind == CARD_MSG && ui.cardReply) { uint32_t r = ui.cardReply; dismissCard(); composeOpen(r); return; }
    dismissCard();
    return;
  }
  if (ui.modal == M_BREACH) return;
  if (ui.modal == M_DOSSIER && (c == 's' || c == 'x' || c == 'm' || c == 'p')) {
    nodeAction(c, ui.target, ui.targetEcho); return;
  }
  if (ui.modal == M_NONE && ui.tab == T_RADAR && (c == 's' || c == 'x' || c == 'm' || c == 'p')) {
    Row rows[1 + MAX_KNOWN_NODES + ECHO_MAX_NODES];
    int n = radarRows(rows, 1 + MAX_KNOWN_NODES + ECHO_MAX_NODES);
    if (n) { int s = radarSel(rows, n); nodeAction(c, rows[s].id, rows[s].echo); }
    return;
  }
  for (int i = 0; i < T_COUNT; i++) if (c == TAB_KEY[i]) { gotoTab(i); return; }
  if (c == 'm') { composeOpen(ui.target); return; }
  if (c == 'b') {
    if (!loraReady) { toast("Radio offline", TT_BAD); return; }
    loraSendBeacon(); toast("Beacon sent", TT_GOOD); sfx(SFX_SEND); return;
  }
  if (c == ' ' && ui.modal == M_NONE) {
    if (ui.tab == T_HOME) pokeAvatar();
    else onSelect();
    return;
  }
  // S and X on HOME go straight at the suggested target.
  if ((c == 's' || c == 'x') && ui.modal == M_NONE && ui.tab == T_HOME) {
    NextMove m = nextMove();
    if (m.kind == NX_DOSSIER) { ui.tab = T_RADAR; ui.selId = m.id; ui.selEcho = false; nodeAction(c, m.id, false); }
    else gotoTab(T_RADAR);
    return;
  }
}

// ─────────────────────────────────────────────
//  Events from the game
// ─────────────────────────────────────────────
static uint32_t idFromHex(const String& s) { return (uint32_t)strtoul(s.c_str(), nullptr, 16); }

void tdeckEvtHack(bool won, const String& tid, int xp, const String& note) {
  uint32_t now = millis();
  String who = (tid == "TRAINING") ? String("TRAINING") : nodeDisplayName(idFromHex(tid));
  av.trigger(won ? AV_R_WIN : AV_R_LOSE, now, 4000);
  sfx(won ? SFX_WIN : SFX_LOSE);
  say(won ? "Too easy." : "They saw me coming.");
  if (won) openCard(CARD_RESULT, "BREACHED", TT_GOOD, who + " is yours.", "+" + String(xp) + " XP", note);
  else     openCard(CARD_RESULT, "COUNTERED", TT_BAD, who + " held.", "-" + String(xp) + " XP", note);
}
void tdeckEvtHackTimeout() {
  av.trigger(AV_R_LOSE, millis(), 2000); sfx(SFX_ERR);
  openCard(CARD_RESULT, "NO ANSWER", TT_WARN, "The target never answered.", "Out of range? Nothing was lost.");
}
void tdeckEvtDefense(uint32_t who, bool breached) {
  uint32_t now = millis();
  String nm = nodeDisplayName(who);
  if (breached) {
    av.trigger(AV_R_LOSE, now, 4000); sfx(SFX_ALERT);
    openCard(CARD_RESULT, "BREACHED!", TT_BAD, nm + " got through your firewall.",
             "Raise FIREWALL on the SKILLS tab.");
  } else {
    av.trigger(AV_R_WIN, now, 3000); sfx(SFX_HELD);
    openCard(CARD_RESULT, "HELD", TT_GOOD, nm + " tried to breach you.", "Your firewall held.");
  }
}
void tdeckEvtMessage(const String& fromId, const String& msg) {
  uint32_t id = idFromHex(fromId);
  av.trigger(AV_R_MESSAGE, millis(), 2500); sfx(SFX_MSG);
  say("\"" + msg + "\"");
  openCard(CARD_MSG, "MESSAGE", TT_ACCENT, msg, "from " + nodeDisplayName(id), "", "", 7000, id);
  if (cardQn) toast("Message from " + nodeDisplayName(id) + " waiting", TT_ACCENT);
}
void tdeckEvtLevelUp() {
  av.trigger(AV_R_LEVELUP, millis(), 5000); sfx(SFX_LEVELUP);
  say("LEVEL " + String(myLevel) + "!");
  openCard(CARD_LEVEL, "LEVEL UP", TT_WARN, "You reached LV " + String(myLevel) + ".",
           "+1 skill point. Press K to spend it.");
}
void tdeckEvtNewNode(uint32_t id) {
  KnownNode* n = findNode(id);
  av.trigger(AV_R_NEWNODE, millis(), 2500); sfx(SFX_NEWNODE);
  String s = "New signal: " + nodeDisplayName(id) + (n ? String(", ") + nodeProximity(n) : String(""));
  toast(s, TT_ACCENT);
  say(n && reconKnows(n, RECON_T_NAME) ? nodeDisplayName(id) + " is back." : String("Signal. Who's that?"));
  wake();
}
void tdeckEvtScouted(uint32_t who) {
  av.trigger(AV_R_ALERT, millis(), 2500); sfx(SFX_ALERT);
  toast("Someone is scouting you: " + nodeDisplayName(who), TT_WARN);
  say("Someone's reading me...");
  wake();
}
void tdeckEvtArmed() {
  if (ui.modal == M_CARD && ui.cardKind == CARD_ARMED) return;
  openCard(CARD_ARMED, "RESET ARMED", TT_BAD, "Hold the trackball down for 5 seconds to erase everything.",
           "Do nothing and it cancels itself.", "", "", 0);
}
void tdeckEvtWiping() {
  openCard(CARD_WIPING, "WIPING...", TT_BAD, "Keep holding to erase.", "Let go to cancel.", "", "", 0);
  render(millis()); pushAll();                 // the hold loop does not wait for a frame
}
void tdeckEvtQr() { toast("Portal: join " + WiFi.softAPSSID() + ", 192.168.4.1", TT_ACCENT); }

// ─────────────────────────────────────────────
//  Setup and the frame loop
// ─────────────────────────────────────────────
// The two canvases, allocated early in setup() — before Wi-Fi can take its
// share of memory — so the 150 KB screen always gets its block.
void tdeckCanvasBegin() {
  if (!scr) scr = new GFXcanvas16(TDECK_W, TDECK_H);
  if (!scr || !scr->getBuffer()) {
    Serial.println("[TDECK] no memory for the screen canvas!");
    if (tft) {
      tft->fillScreen(0);
      tft->setTextColor(0xF800); tft->setTextSize(2);
      tft->setCursor(10, 100); tft->print("OUT OF MEMORY");
      tft->setTextSize(1); tft->setCursor(10, 130);
      tft->print("Enable PSRAM (OPI) and reflash.");
      tdeckBacklight(12);
    }
  }
  av.begin();
}

void tdeckAppBegin() {
  tdeckCanvasBegin();
  av.bg = PAL.bg;
  av.accent = gameConfigured() ? factionColour(myFaction.charAt(0)) : PAL.accent;
  av.mood = cyMood;
  if (!gameConfigured()) { ui.modal = M_SETUP; ui.setupStep = 0; }
  else if (resetArmed) tdeckEvtArmed();          // armed by two short boots
  uint32_t forfeit = breachPendingTake();
  if (forfeit && gameConfigured()) {
    // Switched off in the middle of a breach: it counts as the miss it
    // would most likely have been. (Its card waits behind any other.)
    actHackTimed(forfeit, false);
    toast("Breach cut off by power loss: traced", TT_BAD);
  }
  sndBegin();
  sfx(SFX_BOOT);
  tdeckKbBacklight(KB_DUTY[tdp.kblight]);
  ui.lastInput = millis();
  ui.dirty = true;
  battSample(true);
  if (scr && scr->getBuffer()) { render(millis()); pushAll(); }
  tdeckBacklight(tdp.bright);
}

static uint32_t liveSignature() {
  // Things that change on their own and are shown: if any of these move,
  // the screen is redrawn.
  uint32_t h = 2166136261u;
  auto mix = [&](uint32_t v) { h ^= v; h *= 16777619u; };
  mix(knownCount); mix(echoCount); mix(msgLogCount); mix(eventCount); mix(eventNext);
  mix(myXP); mix(myLevel); mix(skillPoints); mix(loraReady); mix(mailPending());
  mix(loraActionPending() | (hackInFlight << 1)); mix(loraActionTries); mix(loraActionState);
  for (int i = 0; i < knownCount; i++) { mix(knownNodes[i].intel); mix(knownNodes[i].recon_count); mix(knownNodes[i].msg_unread); }
  return h;
}

void prgResync();                                  // cypher32.ino
void tdeckTick() {
  uint32_t now = millis();
  if (!scr || !scr->getBuffer()) return;
  prgResync();

  // ── input ──
  bool input = false;
  for (int i = 0; i < 4; i++) {
    char k = tdeckReadKey();
    if (!k) break;
    tdeckKbLastPoll = 0;                          // there may be more waiting
    bool woke = ui.asleep || ui.dimmed;
    wake(); input = true;
    if (woke) continue;                           // the key that wakes it is only a wake
    onKey(k);
  }
  static uint32_t lastStep = 0;
  // Read and clear together, so a pulse landing in between is not lost.
  noInterrupts();
  int16_t dx = tdeckTbX, dy = tdeckTbY;
  bool moved = abs(dx) >= 2 || abs(dy) >= 2;
  if (moved) { tdeckTbX = 0; tdeckTbY = 0; }
  interrupts();
  // A lone stray count must not linger and add to the next real roll.
  static uint32_t tbQuietSince = 0;
  if (dx || dy) { if (!tbQuietSince) tbQuietSince = now; }
  else tbQuietSince = 0;
  if (!moved && tbQuietSince && now - tbQuietSince > 300) {
    noInterrupts(); tdeckTbX = 0; tdeckTbY = 0; interrupts(); tbQuietSince = 0;
  }
  if (moved) {
    tbQuietSince = 0;
    bool woke = ui.asleep || ui.dimmed;
    wake(); input = true;
    if (!woke && (uint32_t)(now - lastStep) >= 110) {
      lastStep = now;
      if (abs(dx) > abs(dy)) onNav(dx > 0 ? 1 : -1, 0);
      else                   onNav(0, dy > 0 ? 1 : -1);
    }
  }
  // Trackball press: counted by the sketch's PRG ISR (same pin, GPIO0). The
  // breach game reads press edges itself (breachTick); here it is the short
  // press-and-release that means "select".
  if (ui.clickSeen != prgShortSeq) {
    ui.clickSeen = prgShortSeq;
    bool woke = ui.asleep || ui.dimmed;
    wake(); input = true;
    bool gamePress = ui.gameClosedAt && (uint32_t)(millis() - ui.gameClosedAt) < 5000 &&
                     (int32_t)(prgPressMs - ui.gameClosedAt) <= 0;
    if (gamePress) {}                                     // the game already used it
    else if (!woke && !resetArmed && ui.modal != M_BREACHGAME) onSelect();
  }
  // The keyboard is polled faster while the ball is moving.
  tdeckKbFast = (ui.modal == M_BREACHGAME);
  (void)input;

  // Handling input takes time (each keyboard read is an I2C transaction) and
  // stamps things with millis() as it goes — last input, a recon round's
  // start, a breach's start. Everything below must measure against a clock
  // read after that, or "now - stamp" goes negative and wraps to ~49 days:
  // which is exactly how a keypress used to switch the screen off.
  now = millis();

  // ── power ──
  uint16_t tmo = TIMEOUT_S[tdp.timeout];
  uint32_t idle = (int32_t)(now - ui.lastInput) > 0 ? now - ui.lastInput : 0;
  bool busy = ui.modal == M_RECON || ui.modal == M_BREACH || ui.modal == M_SETUP || ui.modal == M_BREACHGAME;
  if (!busy && tmo && idle > (uint32_t)tmo * 1000UL && !ui.asleep) {
    ui.asleep = true; av.night = true; tdeckScreenPower(false);
  } else if (!busy && !ui.dimmed && !ui.asleep && idle > 30000UL && (!tmo || tmo > 30)) {
    ui.dimmed = true; av.night = true; tdeckBacklight(tdp.bright > 4 ? 3 : 1);
  }

  // ── game state that feeds the avatar ──
  battSample();
  av.cold = battPct >= 0 && battPct < 15;
  av.mood = cyMood;
  av.bg = PAL.bg;
  if (gameConfigured()) av.accent = factionColour(myFaction.charAt(0));

  // ── modal timers ──
  if (ui.modal == M_RECON) reconTick(now);
  if (ui.modal == M_BREACHGAME) breachTick(now);
  if (ui.modal == M_CARD) {
    if (ui.cardKind == CARD_ARMED && !resetArmed) dismissCard();
    // Let go = cancelled. Read the pin itself: the ISR's debounce can miss
    // a very short tap and leave its idea of the level stale.
    else if (ui.cardKind == CARD_WIPING && digitalRead(PRG_PIN) == HIGH) {
      if (resetArmed) { ui.modal = M_NONE; tdeckEvtArmed(); } else dismissCard();
    }
    else if (ui.cardUntil && (int32_t)(now - ui.cardUntil) > 0) dismissCard();
  }
  pumpCards();
  if (ui.modal == M_BREACH && !hackInFlight && !hackTimedOut && (int32_t)(now - ui.breachStart) > 1500) {
    ui.modal = ui.target ? M_DOSSIER : M_NONE; ui.dirty = true;   // verdict card will follow
  }
  if (ui.modal == M_DOSSIER && ui.hackArmUntil && (int32_t)(now - ui.hackArmUntil) > 0) { ui.hackArmUntil = 0; ui.dirty = true; }
  if (ui.toastUntil && (int32_t)(now - ui.toastUntil) > 0) { ui.toastUntil = 0; ui.dirty = true; }

  if (ui.asleep) { av.step(now); return; }

  // Live data changed, or a once-a-second refresh for clocks and ages.
  uint32_t sig = liveSignature();
  if (sig != ui.sig) { ui.sig = sig; ui.dirty = true; }
  bool animated = (ui.modal == M_COMPOSE || ui.modal == M_TEXT || ui.modal == M_BREACH ||
                   (ui.modal == M_NONE && ui.tab == T_RADAR && knownCount == 0 && !trainingActive()) ||
                   (ui.modal == M_RECON && rc.phase == RC_LINK));
  // Screens that show a clock (ages, cooldowns, a spinner) refresh on their
  // own; the rest only when something changes. A full push costs ~40 ms of
  // loop() time, so it is not spent on screens that cannot have moved.
  bool clocks = ui.modal == M_DOSSIER || ui.modal == M_DIAG ||
                (ui.modal == M_NONE && (ui.tab == T_RADAR || ui.tab == T_INBOX || ui.tab == T_LOG));
  uint32_t every = animated ? 120u : clocks ? 1000u : 5000u;
  if (now - ui.lastFull > every) ui.dirty = true;

  // ── draw ──
  bool av_on = avatarVisible();
  uint32_t frameMs = ui.dimmed ? 250 : 66;
  if (av_on && now - ui.lastAnim >= frameMs) {
    ui.lastAnim = now;
    av.step(now);
    av.draw(now);
    if (!ui.dirty) {
      blitAvatar(AV_X, AV_Y);
      pushRect(AV_X, AV_Y, AV_W, AV_H);
    }
  } else if (!av_on) av.step(now);

  // While the breach ball is moving, only its strip is drawn: a full-screen
  // push is ~40 ms and would stutter the ball. Anything else that changed is
  // drawn the moment the run ends.
  if (breachAnimate(now)) return;

  if (ui.dirty) {
    ui.dirty = false;
    ui.lastFull = now;
    render(now);
    pushAll();
  }
}
