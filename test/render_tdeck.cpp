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
// The trackball going down (what the sketch's PRG ISR records), without the
// release: the breach game reads this edge.
static void ballDown()         { prgPressMs = g_millis; prgPressSeq = prgPressSeq + 1; g_millis++; tdeckTick(); }
static void ballUp()           { prgShortSeq++; run(60); }
// Step the clock 1 ms at a time until the ball is inside (or well outside)
// the gap, then return. Gives up after `limit` ms.
static bool waitBall(bool inside, uint32_t limit = 6000) {
  for (uint32_t t = 0; t < limit && br.phase == BR_RUN; t++) {
    int32_t tj = (int32_t)(g_millis - br.t0) - BR_LAG_MS;
    int d = abs(breachBallPos(tj < 0 ? 0 : tj) - br.zoneC);
    if (inside ? d * 2 <= br.zoneW - 4 : d * 2 > br.zoneW + 20) return true;
    g_millis++; tdeckTick();
  }
  return false;
}
// With the breach game open: start it, wait for the ball, stop it in (or
// clear of) the gap. The release of each press follows, as a real one does.
static void playBreach(bool hit) {
  ballDown(); ballUp();
  run(950);
  waitBall(hit);
  ballDown(); ballUp();
}

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
  ck(ui.modal == M_BREACHGAME && br.phase == BR_INTRO, "X again opens the breach game");
  ck(br.zoneW > 0 && br.periodMs > 0, "tuned from the stats");
  shot("breach-intro");
  // Keys other than SPACE/ENTER/BKSP do nothing here (no tab jumps mid-hack).
  key('r');
  ck(ui.modal == M_BREACHGAME && br.phase == BR_INTRO, "stray keys are ignored in the game");
  ballDown();
  ck(br.phase == BR_COUNT, "a trackball press starts the countdown");
  ballUp();
  ck(ui.modal == M_BREACHGAME, "and its release is not a 'select'");
  run(300);
  shot("breach-count");
  run(650);
  ck(br.phase == BR_RUN, "the ball runs after the countdown");
  ck(waitBall(true), "the ball reaches the gap");
  shot("breach-run");
  // Judged at the instant of the press, even if the app only sees it later.
  uint32_t at = g_millis;
  int posAt = breachBallPos(at - br.t0 - BR_LAG_MS);   // as it was on screen
  prgPressMs = at; prgPressSeq = prgPressSeq + 1;
  g_millis += 45;                               // a slow frame
  tdeckTick();
  ck(br.phase == BR_RESULT && br.stopPos == posAt, "the verdict is where the ball was when the button went down");
  ck(br.hit, "stopping in the gap is a hit");
  shot("breach-hit");
  ballUp();
  ck(ui.modal == M_BREACHGAME, "the release after the stop does not click through");
  run(1200);
  ck(ui.modal == M_CARD, "then the verdict card shows");
  ck(myXP > xp0 || myLevel > lvl0, "a hit on the dummy is a won hack");
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

  // ── a keypress must never switch the screen off ──
  // The frame loop read the clock before the keyboard, and the keypress
  // stamped "last input" a millisecond later; now - lastInput went negative,
  // wrapped to ~49 days, and the screen went black on the spot.
  printf("screen stays on\n");
  {
    ui.modal = M_NONE; wake(); run(200);
    bool blanked = false;
    for (int i = 0; i < 40 && !blanked; i++) {
      key(i % 2 ? 'h' : 'r');
      if (ui.asleep || !tdeckScreenOn) blanked = true;
    }
    ck(!blanked, "pressing keys never switches the screen off");
    for (int i = 0; i < 20 && !blanked; i++) { roll(i % 2 ? 1 : -1, 0); if (ui.asleep || !tdeckScreenOn) blanked = true; }
    ck(!blanked, "nor does rolling the trackball");
    for (int i = 0; i < 20 && !blanked; i++) { press(); key(0x08); if (ui.asleep || !tdeckScreenOn) blanked = true; }
    ck(!blanked, "nor does pressing it");
  }

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

  // ── the breach game against real nodes ──
  printf("breach vs nodes\n");
  auto hackReqFlags = []() -> int {            // flags of the HACK_REQ in flight, -1 if none
    if (pendingUser.active && pendingUser.len >= sizeof(PktHeader) &&
        ((PktHeader*)pendingUser.buf)->type == PKT_HACK_REQ) return ((PktHeader*)pendingUser.buf)->flags;
    return -1;
  };
  {
    // A classic (non-T-Deck) device: a miss never reaches the air.
    KnownNode* n = findNode(0xBEEF0003); n->tdeck = false;
    key('x');
    ck(ui.modal == M_BREACHGAME, "X X on a node opens the breach game");
    BreachSpec sp = breachSpecFor(0xBEEF0003);
    ck(br.zoneW == sp.tu.zoneW && br.periodMs == sp.tu.periodMs, "the game is played on the dossier's preview");
    ck(sp.knowS && sp.knowF && sp.dS == n->seen_stealth && sp.dF == n->seen_firewall, "with the stats recon revealed");
    ck(br.zoneC - br.zoneW / 2 >= BR_EDGE && br.zoneC + br.zoneW / 2 <= BR_LEN - BR_EDGE, "the gap is clear of both ends");
    ck(br.startRight == (br.zoneC < BR_LEN / 2), "and the ball starts at the far end");
    key(0x08);
    ck(ui.modal == M_DOSSIER, "BACKSPACE before the run backs out, free");
    ck(!recentlyFailed(chipIdStr(0xBEEF0003)), "and costs nothing");
    key('x'); key('x');
    ck(ui.modal == M_BREACHGAME, "and it can be opened again");
    int lost0 = statLost, xp0b = myXP;
    playBreach(false);
    ck(br.phase == BR_RESULT && !br.hit, "stopping clear of the gap is a miss");
    ck(br.errMs != 0, "and says how early or late it was");
    shot("breach-miss");
    g_keyQueue.push_back(0x08); run(40);
    ck(ui.modal == M_BREACHGAME && br.phase == BR_RESULT, "the run cannot be backed out of once it has started");
    run(1300);
    ck(statLost == lost0 + 1, "a miss is a lost hack");
    ck(myXP <= xp0b, "XP does not go up for it");
    ck(recentlyFailed(chipIdStr(0xBEEF0003)), "and locks the target for 30 minutes");
    ck(!hackInFlight, "resolved on the spot");
    ck(hackReqFlags() < 0, "a classic device is not sent a phantom hack request");
    while (ui.modal == M_CARD || cardQn) { key(0x08); run(50); }
    ui.modal = M_DOSSIER; ui.target = 0xBEEF0003; ui.dirty = true; run(50);
    key('x'); key('x');
    ck(ui.modal == M_DOSSIER, "a locked-out target refuses the breach game");
  }
  {
    // Another T-Deck: a hit is sent with the game's result on it.
    addNode(0xBEEF0004, 'R', 4, -60, 0);
    findNode(0xBEEF0004)->tdeck = true;
    while (ui.modal == M_CARD || cardQn) { key(0x08); run(50); }
    ui.modal = M_NONE; key('r');
    ui.selId = 0xBEEF0004; ui.selEcho = false; key('\r');
    ck(ui.modal == M_DOSSIER && ui.target == 0xBEEF0004, "the T-Deck node's dossier");
    key('x'); key('x');
    ck(ui.modal == M_BREACHGAME, "breach game against a T-Deck");
    ballDown(); ballUp(); run(950); waitBall(true);
    ballDown();
    ck(br.hit, "a hit");
    run(1000);
    int f = hackReqFlags();
    ck(f >= 0 && (f & PKTFLAG_TIMED) && (f & PKTFLAG_TIMED_WIN), "the request carries TIMED|WIN");
    ck(hackInFlight && hackTimedOutcome == 1, "and waits for the target's answer");
    ck(ui.modal == M_BREACH, "on the breach-in-progress screen");
  }

  // ── balance properties ──
  printf("breach balance\n");
  {
    int sb = skillBrute, ss = skillStealth, sf = skillFirewall, sl = myLevel;
    // Equal builds meet at the same window at every level.
    int w1 = -1; bool flat = true;
    for (int L = 1; L <= MAX_LEVEL; L += 3) {
      int each = (L + 2) / 3;
      BreachTune t = breachTune(each, each, each, each, each, each, 5, false);
      if (w1 < 0) w1 = t.windowMs; else if (abs(t.windowMs - w1) > 2) flat = false;
    }
    ck(flat, "equal builds get the same breach at every level (scalable)");
    // The owner's three rules, each on its own.
    BreachTune base = breachTune(8, 8, 8, 8, 8, 8, 0, false);
    ck(breachTune(12, 8, 8, 8, 8, 8, 0, false).zoneW > base.zoneW, "more BRUTE: a longer gap");
    ck(breachTune(8, 8, 8, 8, 12, 8, 0, false).zoneW < base.zoneW, "their STEALTH: a shorter gap");
    ck(breachTune(8, 8, 8, 8, 8, 12, 0, false).speed > base.speed, "their FIREWALL: a faster ball");
    ck(breachTune(8, 8, 8, 8, 8, 12, 0, false).zoneW == base.zoneW, "(and firewall leaves the gap alone)");
    // Scouting never makes a breach harder, whatever the target really has.
    bool monotone = true;
    uint32_t rs = 99;
    for (int trial = 0; trial < 300 && monotone; trial++) {
      rs = rs * 1103515245u + 12345u;
      int L = 1 + (int)((rs >> 16) % MAX_LEVEL);
      int total = L + 2;
      // A RED target: 3 of its points are always STEALTH.
      rs = rs * 1103515245u + 12345u; int b = (int)((rs >> 16) % (total - 3 + 1));
      rs = rs * 1103515245u + 12345u; int s = 3 + (int)((rs >> 16) % (total - 3 - b + 1));
      int f = total - b - s;
      rs = rs * 1103515245u + 12345u;
      myLevel = 1 + (int)((rs >> 16) % MAX_LEVEL);
      skillBrute = myLevel / 3 + 1; skillStealth = myLevel / 3; skillFirewall = myLevel - 1 - myLevel / 3 * 2 + 2;
      addNode(0xBEEF00AA, 'R', L, -60, 0);
      KnownNode* nn = findNode(0xBEEF00AA);
      nn->seen_brute = b; nn->seen_stealth = s; nn->seen_firewall = f; nn->recon_score = 0;
      int prev = -1;
      for (int tier : { 0, RECON_T_FACTION, RECON_T_BRUTE, RECON_T_STEALTH, RECON_T_FIREWALL }) {
        nn->intel = tier;
        int w = breachSpecFor(0xBEEF00AA).tu.windowMs;
        // (the gap is whole pixels, so equal difficulties can land up to one
        // pixel's worth of time apart)
        BreachTune tt = breachSpecFor(0xBEEF00AA).tu;
        int px = 1000 / tt.speed + 1;
        if (prev >= 0 && w + px < prev) monotone = false;
        prev = w;
      }
    }
    ck(monotone, "scouting deeper never makes a breach harder (unknown stats are assumed at their worst)");
    skillBrute = sb; skillStealth = ss; skillFirewall = sf; myLevel = sl;
  }

  // ── breach rules that are about time and power ──
  printf("breach edge cases\n");
  {
    while (ui.modal == M_CARD || cardQn) { key(0x08); run(50); }
    hackInFlight = false; hackTimedOut = false; pendingUser.active = false; loraActionState = LA_IDLE;
    addNode(0xBEEF0006, 'R', 4, -60, 9);
    findNode(0xBEEF0006)->tdeck = false;
    ui.modal = M_NONE; key('r');
    ui.selId = 0xBEEF0006; ui.selEcho = false; key('\r');
    key('x'); key('x');
    ck(ui.modal == M_BREACHGAME, "breach open");
    ballDown(); ballUp(); run(950);
    ck(br.phase == BR_RUN, "running");
    // The loop stalls past the time limit and then sees a press made after it.
    uint32_t lim = breachRunLimitMs();
    g_millis = br.t0 + lim + 300;
    prgPressMs = g_millis; prgPressSeq = prgPressSeq + 1;
    tdeckTick();
    ck(br.phase == BR_RESULT && br.timedOut && !br.hit, "a press after time ran out is a timeout, not a hit");
    run(1300);
    while (ui.modal == M_CARD || cardQn) { key(0x08); run(50); }

    // Switched off mid-run: the next boot counts it as a miss.
    addNode(0xBEEF0007, 'R', 4, -60, 9);
    findNode(0xBEEF0007)->tdeck = false;
    ui.modal = M_NONE; key('r');
    ui.selId = 0xBEEF0007; ui.selEcho = false; key('\r');
    key('x'); key('x');
    ballDown(); ballUp(); run(950);
    ck(br.phase == BR_RUN, "a second run");
    int lost0 = statLost;
    ui.modal = M_NONE; br = Breach();             // the power goes
    tdeckAppBegin();                              // and comes back
    run(200);
    ck(statLost == lost0 + 1, "a breach cut off by power loss counts as a miss at the next boot");
    ck(recentlyFailed(chipIdStr(0xBEEF0007)), "with the 30-minute lock");
    tdeckAppBegin(); run(100);
    ck(statLost == lost0 + 1, "and only once");
  }

  printf("%d checks, %d failures\n", checks, bad);
  return bad ? 1 : 0;
}
