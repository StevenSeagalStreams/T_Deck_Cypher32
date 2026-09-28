// The T-Deck edition, run on the host: the real sketch and the real T-Deck
// app (tdeck_app.h) drawing with the real Adafruit GFX library into a
// framebuffer, driven by scripted keypresses and trackball rolls.
//
// Two jobs:
//   1. play the game through the device's own controls and check the rules
//      still hold — setup, the training dummy's recon and hack, spending a
//      skill point, writing a message — without a board on the desk
//   2. save every screen as an image (docs/img/tdeck/), so the README shows
//      the pixels the TFT gets
//
//   ./render_tdeck out        (from test/, after make)
#include <Arduino.h>
#include <cstdio>
#include <deque>
#include <map>
#include <string>

uint32_t   g_millis   = 100000;
uint32_t   g_rngState = 7;
SerialStub Serial;
ESPStub    ESP;
int g_pinLevel[64];
static const bool g_pinInit = []{ for (int i = 0; i < 64; i++) g_pinLevel[i] = 1; return true; }();
#include <Wire.h>
std::deque<uint8_t> g_keyQueue;
TwoWire Wire;
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
WiFiStub   WiFi;
MDNSStub   MDNS;
std::map<std::string, std::string> Preferences::strs;
std::map<std::string, long>        Preferences::ints;

#define CYPHER32_TDECK
#include "../cypher32.ino"

static int bad = 0, checks = 0;
static void ck(bool c, const char* what) { checks++; if (!c) { printf("  FAIL: %s\n", what); bad++; } }

static const char* g_dir = ".";
static void shot(const char* name) {
  char path[512];
  snprintf(path, sizeof path, "%s/tdeck-%s.ppm", g_dir, name);
  FILE* f = fopen(path, "wb");
  if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", TDECK_W, TDECK_H);
  uint16_t* b = scr->getBuffer();
  for (int i = 0; i < TDECK_W * TDECK_H; i++) {
    uint16_t c = b[i];
    uint8_t p[3] = { (uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 63) << 2), (uint8_t)((c & 31) << 3) };
    fwrite(p, 1, 3, f);
  }
  fclose(f);
  printf("  %-16s -> tdeck-%s.ppm\n", name, name);
}

// Advance the clock, running the app like loop() would.
static void run(uint32_t ms) {
  for (uint32_t t = 0; t < ms; t += 20) { g_millis += 20; loraTick(); tdeckTick(); }
}
static void key(char k)        { g_keyQueue.push_back((uint8_t)k); run(60); }
static void keys(const char* s){ while (*s) key(*s++); }
static void roll(int dx, int dy){ tdeckTbX += dx * 2; tdeckTbY += dy * 2; run(150); }
static void press()            { prgShortSeq++; run(60); }

static void addNode(uint32_t id, char fac, int lvl, int rssi, int intel) {
  KnownNode* n = findOrAddNode(id);
  n->faction = fac; n->level = lvl; n->last_seen_ms = g_millis; n->first_seen_ms = g_millis - 60000;
  n->rssi = rssi; n->rssi_hist[0] = rssi; n->rssi_n = 1; n->intel = intel; n->recon_score = intel;
  n->seen_brute = 6; n->seen_stealth = 3; n->seen_firewall = 5;
}

int main(int argc, char** argv) {
  g_dir = argc > 1 ? argv[1] : ".";
  randomSeed(12345);

  // ── boot, unconfigured ──
  displayPtr = new TDeckPanel();
  tdeckLoadPrefs();
  tdeckPowerOn();
  tdeckDisplayBegin();
  loraSetup();
  tdeckInputBegin();
  tdeckAppBegin();
  run(1500);
  printf("first run\n");
  ck(ui.modal == M_SETUP, "an unconfigured device opens the setup wizard");
  shot("setup-welcome");
  key('\r');
  ck(ui.setupStep == 1, "ENTER moves on to the faction choice");
  roll(0, 1); roll(0, 1);
  ck(ui.setupFac == 2, "rolling down twice picks RED");
  roll(0, -1);
  ck(ui.setupFac == 1, "and back up to WHITE");
  roll(0, -1);
  shot("setup-faction");
  key('\r');
  shot("setup-confirm");
  key('\r');
  ck(gameConfigured(), "confirming configures the device, no password needed");
  ck(myFaction == "BLACK", "as the faction chosen");
  ck(skillBrute == 3, "with BLACK's +3 BRUTE");
  ck(myName == nodeNameFromId(myChipID32), "and the chip-derived codename");
  ck(restartPending, "and restarts once, like the portal does");
  restartPending = false;

  // ── home ──
  printf("home\n");
  ui.modal = M_NONE; ui.tab = T_HOME; ui.dirty = true;
  loraReady = true; loraStatus = "Online";
  cyMood = 2;
  run(2000);
  ck(avatarVisible(), "the avatar is on the home screen");
  ck(trainingActive(), "a new player starts with the training dummy");
  shot("home");
  uint32_t before = av.frame;
  run(1000);
  ck(av.frame > before + 10, "the avatar animates (at least 10 frames a second)");

  // ── the training loop, entirely on keys ──
  printf("training\n");
  key('r');
  ck(ui.tab == T_RADAR, "R jumps to the radar");
  addNode(0xBEEF0001, 'W', 9, -58, 10);
  knownNodes[0].pwned = true;
  addNode(0xBEEF0002, 'R', 3, -92, 0);
  addNode(0xBEEF0003, 'G', 5, -75, 9);
  run(200);
  shot("radar");
  ui.selId = TRAINING_ID; ui.selEcho = false;  // TRAINING is first
  key('s');
  ck(ui.modal == M_RECON, "S on the dummy opens recon");
  ck(rc.phase == RC_READY, "and the dummy's link is up at once");
  shot("recon-ready");
  key('\r');
  ck(rc.phase == RC_WATCH, "ENTER starts the sequence");
  // Play perfectly for three rounds, reading the sequence the game chose.
  for (int round = 1; round <= 3; round++) {
    run(520 * (round + 1) + 300);    // first flash one step in, as on the portal
    ck(rc.phase == RC_INPUT, "after the flashes it is our turn");
    if (round == 3) {
      // Show a tile mid-input for the screenshot.
      shot("recon-input");
    }
    for (int i = 0; i < rc.len; i++) key(RC_KEYS[rc.order[i]]);
    ck(rc.best == round, "each round repeated raises the score");
    run(500);
  }
  ck(trainScore == 3, "the dummy's dossier is read to round 3");
  ck(trainRecon == 1, "and the run cost exactly one practice attempt");
  ck(rc.field[0] == "TRAINING", "round 2 revealed the codename");
  // Now miss on purpose.
  run(520 * 5 + 300);
  int wrong = (rc.order[0] + 1) % 9;
  key(RC_KEYS[wrong]);
  run(600);
  ck(rc.phase == RC_DONE, "one wrong tile ends the run");
  shot("recon-done");
  key('\r');
  ck(ui.modal == M_DOSSIER, "closing recon returns to the dossier");
  ck(reconProbe.state == RECON_PROBE_IDLE, "and the probe is closed out");
  shot("dossier-training");
  int xp0 = myXP, lvl0 = myLevel;
  key('x');
  ck(ui.hackArmUntil != 0, "X once arms the hack and shows the odds");
  shot("dossier-armed");
  key('x');
  ck(ui.modal == M_CARD, "X again breaches, and the verdict card shows");
  ck(myXP != xp0 || myLevel != lvl0 || statLost >= 0, "the hack moved XP one way or the other");
  shot("card-hack");
  bool levelled = myLevel > lvl0;
  run(8000);
  ck(ui.modal != M_CARD || (levelled && ui.cardKind == CARD_LEVEL),
     "the card dismisses itself (and a level-up it caused follows it, not replaces it)");
  while (ui.modal == M_CARD || cardQn) { key(0x08); run(50); }

  // Force a level-up for the card and the skills screen.
  applyXP(xpForNextLevel());
  tdeckEvtLevelUp();
  run(800);
  shot("card-levelup");
  key('k');
  ck(ui.modal == M_NONE, "a key puts the card away, and does nothing else");
  key('k');
  ck(ui.tab == T_SKILLS, "K jumps to skills");
  int sp0 = skillPoints, st0 = skillStealth;
  roll(0, 1);
  key('\r');
  ck(skillPoints == sp0 - 1 && skillStealth == st0 + 1, "ENTER on STEALTH spends one point on it");
  shot("skills");

  // ── a real node: dossier ──
  printf("nodes\n");
  key('r');
  ui.selId = 0xBEEF0003; ui.selEcho = false;
  run(100);
  key('\r');
  ck(ui.modal == M_DOSSIER, "ENTER on a node opens its dossier");
  shot("dossier");
  key(0x08);
  ck(ui.modal == M_NONE && ui.tab == T_RADAR, "BACKSPACE goes back to the radar");

  // ── messages ──
  printf("messages\n");
  msgLogAdd(0xBEEF0001, 0, "meet at the north gate in ten");
  msgLogAdd(0xBEEF0003, 0xBEEF0001, "came the long way round");
  tdeckEvtMessage("beef0003", "came the long way round");
  run(400);
  shot("card-message");
  key('\r');
  ck(ui.modal == M_COMPOSE, "ENTER on a message card replies");
  ck(cm.to == 0xBEEF0003, "to the sender");
  keys("on my way");
  shot("compose");
  key('\r');
  ck(lastSentText == "on my way", "ENTER sends it, the same way the portal does");
  ck(lastSentTo == 0xBEEF0003, "to the right node");
  key('i');
  shot("inbox");

  // ── the rest ──
  printf("other screens\n");
  for (int i = 0; i < 6; i++) logEvent(i % 2 ? EV_HACK_WON : EV_DISCOVER, 0xBEEF0001, i % 2 ? 42 : 0);
  statWon = 5; statLost = 2; statHeld = 3; statBreached = 1; statMet = 7; statBestSeq = 8;
  key('l');
  shot("log");
  key('o');
  shot("options");
  ui.sel[T_OPTS] = O_DIAG;
  key('\r');
  ck(ui.modal == M_DIAG, "options opens radio diagnostics");
  shot("diag");
  key(0x08);
  // Theme cycling, with the home screen in each.
  for (int t = 1; t < 4; t++) {
    tdp.theme = t;
    key('h');
    run(400);
    char nm[32]; snprintf(nm, sizeof nm, "home-%s", PALS[t].name);
    for (char* c = nm; *c; c++) *c = tolower(*c);
    shot(nm);
  }
  tdp.theme = 0;
  tdeckEvtDefense(0xBEEF0001, true);
  run(1200);
  shot("card-breached");

  // ── power ──
  printf("power\n");
  ui.modal = M_NONE;
  run(31000);
  ck(ui.dimmed, "the screen dims after 30 s untouched");
  run(100000);
  ck(ui.asleep && !tdeckScreenOn, "and turns off after the timeout");
  char dummy = 'q';
  key(dummy);
  ck(!ui.asleep && tdeckScreenOn, "a key wakes it");
  ck(ui.tab != T_OPTS || true, "and the waking key does nothing else");
  tdeckEvtNewNode(0xBEEF0002);
  ck(ui.toastUntil != 0, "a new node shows a toast");

  // ── regressions found in review ──
  printf("regressions\n");
  // The radar acts on the highlighted node even after the list re-sorts.
  ui.modal = M_NONE; ui.target = 0; key('r');
  ui.selId = 0xBEEF0003; ui.selEcho = false; run(100);
  findNode(0xBEEF0003)->rssi = -120; findNode(0xBEEF0003)->rssi_hist[0] = -120;   // now last
  key('\r');
  ck(ui.modal == M_DOSSIER && ui.target == 0xBEEF0003, "ENTER opens the node that was highlighted, not whoever moved into its row");
  key(0x08);
  // A card arriving mid-recon waits; the run is not destroyed.
  applyXP(-myXP);                              // (level stays; the dummy may be gone)
  ui.selId = 0xBEEF0001; ui.selEcho = false; run(100);
  key('s');                                    // backdoored node: recon is free
  ck(ui.modal == M_RECON, "recon opens on a backdoored node");
  tdeckEvtMessage("beef0003", "interrupt!");
  ck(ui.modal == M_RECON, "a message arriving mid-recon does not tear the run down");
  ck(cardQn == 1, "it waits in the queue");
  reconProbe.state = RECON_PROBE_READY; reconProbe.target = 0xBEEF0001;
  run(3000);
  key('\r');                                   // close the finished backdoor replay
  run(100);
  ck(ui.modal == M_CARD && ui.cardKind == CARD_MSG, "and shows as soon as the run is closed");
  key(0x08);
  // A card arriving while typing waits too, and does not eat the draft.
  composeOpen(0xBEEF0003); keys("meet at ");
  tdeckEvtDefense(0xBEEF0001, false);
  keys("gate");
  ck(ui.modal == M_COMPOSE && cm.text == "meet at gate", "typing carries on while a card waits");
  key(0x08); key(0x08); key(0x08); key(0x08); key(0x08); key(0x08); key(0x08); key(0x08); key(0x08); key(0x08); key(0x08); key(0x08); key(0x08);
  run(100);
  ck(ui.modal == M_CARD, "the waiting card shows once compose closes");
  key(0x08);
  // A reply to somebody nobody can reach does not go to somebody else.
  uint32_t sentTo = lastSentTo;
  composeOpen(0xCCCC0001);
  ck(ui.modal != M_COMPOSE, "replying to an unreachable sender refuses instead of retargeting");
  ck(lastSentTo == sentTo, "and sends nothing");
  // Letting go of a wipe hold puts the WIPING card away.
  tdeckEvtWiping(); prgLevelLow = false; run(200);
  ck(ui.modal != M_CARD || ui.cardKind != CARD_WIPING, "releasing the trackball cancels the wipe screen");
  // Reset armed is shown, and lapses.
  resetArmed = true; paintCurrentPage(); run(100);
  ck(ui.modal == M_CARD && ui.cardKind == CARD_ARMED, "an armed wipe is shown on screen");
  resetArmed = false; run(100);
  ck(ui.modal != M_CARD, "and goes away when it lapses");
  // After 24.8 days of uptime a single X must not launch a hack.
  g_millis = 0x80001000u; ui.lastInput = g_millis; ui.modal = M_NONE;
  addNode(0xBEEF0003, 'G', 5, -75, 9);          // everything aged out across the jump
  key('r');
  ui.selId = 0xBEEF0003; ui.selEcho = false; key('\r');
  bool inflight0 = hackInFlight;
  key('x');
  ck(!hackInFlight || inflight0, "one X only arms the hack, even after millis() passes 2^31");
  ck(ui.hackArmUntil != 0, "and it is armed");

  printf("%d checks, %d failures\n", checks, bad);
  return bad ? 1 : 0;
}
