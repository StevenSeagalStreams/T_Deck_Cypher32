#pragma once
// ─────────────────────────────────────────────
//  BREACH — the timing game that decides a hack
// ─────────────────────────────────────────────
//
//  A thin line with a thick stretch on it — the gap in their firewall. A ball
//  sweeps back and forth; stop it inside the gap. Hit and you are in; miss
//  and you are traced, which is a failed hack.
//
//  The shape of it comes from the fight:
//    your BRUTE        widens the gap        (it is what you force it open with)
//    their STEALTH     narrows it            (they hide how big the hole is)
//    their FIREWALL    speeds the ball up    (it is fighting back)
//    your STEALTH      slows it down         (you slip in quietly)
//  with the exact numbers in breachTune() below — see the balance notes there.
//
//  JUDGED BY THE TIMESTAMP, NOT THE FRAME. The trackball press is timed by an
//  interrupt at the instant the button goes down (prgPressMs), and the ball's
//  position is a pure function of time, so the verdict is where the ball was
//  at that instant. A slow frame, a radio packet being handled or the screen
//  being pushed cannot move the ball under your finger. SPACE and ENTER work
//  too, but the keyboard is scanned by its own chip, which adds a delay (allowed
//  for, below) and jitter (which cannot be), so the trackball is the precise
//  control.
//
//  Included from tdeck_app.h; everything it touches is defined there.

enum { BR_INTRO, BR_COUNT, BR_RUN, BR_RESULT };

struct Breach {
  uint32_t target = 0;
  uint8_t  phase = BR_INTRO;
  // tuning, fixed for the attempt
  int      zoneW = 40;            // px, the gap
  int      zoneC = 160;           // px along the bar, its centre
  int      coreW = 8;             // px, the dead-centre (cosmetic "clean breach")
  uint32_t periodMs = 1400;       // one full sweep there and back
  int      maxSweeps = 3;         // half-sweeps allowed before it times out
  bool     startRight = false;
  int      speed = 450;           // px/s
  int      windowMs = 60;         // time the ball spends in the gap per pass
  // inputs, kept for the screen
  BreachSpec spec;
  // run
  uint32_t t0 = 0, countT0 = 0, resultT = 0;
  uint32_t pressSeqSeen = 0;
  int      stopPos = -1;          // where the ball was when you pressed
  int      errMs = 0;             // + late, - early, from the gap's centre
  bool     hit = false, clean = false, timedOut = false, committed = false, sent = false;
  bool     voided = false;
  uint32_t openedAt = 0;        // a hit that could not be sent: nothing counted
  int      countStep = -1;        // last countdown number shown
  int      dotsShown = -1;        // passes-left dots last drawn during the run
  // drawing
  int      lastBallX = -1;
  uint32_t lastPush = 0;
} br;

// Bar geometry (screen px). BR_LEN, BreachTune and breachSpecFor() are in
// tdeck_app.h, which the dossier's preview shares.
#define BR_X0   20
#define BR_Y    132

// Where the ball is at time t (ms since the run started), 0..BR_LEN.
// A triangle wave: out and back in one period.
static int breachBallPos(uint32_t t) {
  uint32_t p = br.periodMs;
  uint32_t ph = (uint32_t)(((uint64_t)t * 2 * BR_LEN) / p) % (2 * BR_LEN);
  int x = ph < BR_LEN ? (int)ph : (int)(2 * BR_LEN - ph);
  return br.startRight ? BR_LEN - x : x;
}
// +1 while the ball is moving right, -1 left.
static int breachBallDir(uint32_t t) {
  uint32_t ph = (uint32_t)(((uint64_t)t * 2 * BR_LEN) / br.periodMs) % (2 * BR_LEN);
  int d = ph < BR_LEN ? 1 : -1;
  return br.startRight ? -d : d;
}
// Time into the run; a `now` read before the run started counts as its start.
static uint32_t breachRunT(uint32_t now) {
  int32_t t = (int32_t)(now - br.t0);
  return t < 0 ? 0 : (uint32_t)t;
}
static uint32_t breachRunLimitMs() { return (uint32_t)br.maxSweeps * br.periodMs / 2; }

// What you see is behind the ball's true position: up to 10 ms until the
// next strip push, ~3.5 ms to send it, and up to 16.7 ms for the panel's own
// 60 Hz scan to reach it — ~13 ms on average. A press is judged against the
// ball as it was on screen. The keyboard adds its own chip's scan, the 4 ms
// poll and the I2C read — an estimated ~22 ms more on average (measure with a
// high-speed video to refine) (its jitter cannot be taken
// out, which is why the trackball, timed by an interrupt, is the precise
// control).
#define BR_LAG_MS      13
#define BR_KEY_LAG_MS  22

// Open the game against a target that has passed every rule in actHackCheck().
static void breachOpen(uint32_t id) {
  br = Breach();
  br.target = id;
  br.spec = breachSpecFor(id);
  const BreachTune& t = br.spec.tu;
  br.zoneW = t.zoneW; br.coreW = t.coreW; br.periodMs = t.periodMs; br.maxSweeps = t.maxSweeps;
  br.speed = t.speed; br.windowMs = t.windowMs;
  // The gap lands somewhere new every attempt, but never near an end: there
  // the ball turns round and crosses it twice in a row, a free second chance.
  int lo = BR_EDGE + br.zoneW / 2, hi = BR_LEN - BR_EDGE - br.zoneW / 2;
  br.zoneC = hi > lo ? avRange(lo, hi) : BR_LEN / 2;
  // The ball starts at the far end, so the first pass is always a long,
  // readable run-up and never a surprise.
  br.startRight = br.zoneC < BR_LEN / 2;
  br.pressSeqSeen = prgPressSeq;
  br.phase = BR_INTRO; br.openedAt = millis();
  ui.toastUntil = 0;                           // nothing on top of the bar
  ui.modal = M_BREACHGAME; ui.dirty = true;
  av.trigger(AV_R_SCAN, millis(), 3000);
  sfx(SFX_CLICK);
}

// Hand the verdict to the game rules, once. A hit starts the hack (or wins
// the practice round); a miss is a failed hack, applied on the spot.
static void breachApply() {
  if (br.committed) return;
  br.committed = true;
  breachPendingSave(0);                        // resolved: nothing to forfeit at boot
  ActResult r = actHackTimed(br.target, br.hit);
  if (r.code != 200) {                         // a hit that can no longer go out
    br.voided = true;
    toast(r.msg, TT_BAD); sfx(SFX_ERR);
    return;
  }
  br.sent = !r.instant;
}

// The verdict, from where the ball was when you pressed (-1: never pressed).
static void breachJudge(int pos, bool timedOut, int dir) {
  if (br.phase != BR_RUN) return;
  br.stopPos = pos;
  br.timedOut = timedOut;
  // How early or late that was, in ms from the centre of the gap: the number
  // that teaches you your own timing.
  br.errMs = (pos >= 0 && dir) ? (pos - br.zoneC) * dir * 1000 / br.speed : 0;
  // Exactly the pixels drawn: the gap is zoneW px from zx, its core coreW px.
  int zx = br.zoneC - br.zoneW / 2, cx = br.zoneC - br.coreW / 2;
  br.hit   = !timedOut && pos >= zx && pos < zx + br.zoneW;
  br.clean = br.hit && pos >= cx && pos < cx + br.coreW;
  br.phase = BR_RESULT; br.resultT = millis();
  // The verdict counts NOW, not after the result has been shown: a miss
  // cannot be dodged by switching off, a radio hiccup or another action
  // landing in the pause. Only the screen change waits.
  breachApply();
  // A miss and a practice round have already been scored, and the verdict
  // screen that raised plays its own sound; a hit on its way plays one here.
  if (br.sent) { sfx(br.clean ? SFX_PERFECT : SFX_WIN); av.trigger(AV_R_WIN, millis(), 1500); }
  ui.dirty = true;
}

// A press at `at` (ms clock), judged against the ball as it was on screen
// `lagMs` earlier. A press before the ball has moved counts as its start.
static void breachPress(uint32_t at, uint32_t lagMs) {
  int32_t t = (int32_t)(at - br.t0) - (int32_t)lagMs;
  if (t < 0) t = 0;                            // on the very first frame
  // Time ran out before this press (the loop was busy when the limit
  // passed): it is the timeout it would have been.
  if ((uint32_t)t >= breachRunLimitMs()) { breachJudge(-1, true, 0); return; }
  breachJudge(breachBallPos((uint32_t)t), false, breachBallDir((uint32_t)t));
}

// Leave the game for `m`. A trackball press that went down before this
// moment belonged to the game: its release must not select anything.
static void breachClose(uint8_t m) {
  ui.gameClosedAt = millis();
  ui.modal = m; ui.dirty = true;
}

// Once the result has been seen: on to the breach-in-progress screen (a hit
// on its way to a real target) or back to the dossier, where the verdict
// card that is already queued shows at once.
static void breachLeave() {
  if (ui.modal != M_BREACHGAME) return;
  if (br.sent) {
    ui.breachName = nodeDisplayName(br.target);
    ui.breachStart = millis();
    breachClose(M_BREACH);
  } else breachClose(ui.target ? M_DOSSIER : M_NONE);
}

// Start the countdown. From here the attempt can only end in a verdict: if
// the device is switched off before one, it counts as a miss at the next
// boot (tdeckAppBegin), so pulling the battery is not a way out of a bad run.
static void breachBegin(uint32_t now) {
  // Something may have changed since the game opened (the radio, another
  // action): better to say so now than after a hit.
  ActResult ok = actHackCheck(br.target);
  if (ok.code != 200) { toast(ok.msg, TT_BAD); sfx(SFX_ERR); breachClose(ui.target ? M_DOSSIER : M_NONE); return; }
  br.phase = BR_COUNT; br.countT0 = now; br.countStep = -1;
  breachPendingSave(br.target);
  ui.dirty = true; sfx(SFX_CLICK);
}

// Every tick while the game is up.
static void breachTick(uint32_t now) {
  // The clock first, so a press is judged against the phase it happened in.
  switch (br.phase) {
    case BR_COUNT: {
      int step = (int)((now - br.countT0) / 300);        // 0,1,2 = "3","2","1"
      if (step >= 3) {
        // GO. Draw the whole screen for the run once, now, before the clock
        // starts — during the run only the bar is pushed.
        br.phase = BR_RUN; br.t0 = now;              // (the ball drawn at its start)
        render(now); pushAll();
        br.t0 = millis(); br.lastPush = br.t0 - 10; br.dotsShown = -1;
        ui.dirty = false;
        sndPlay(1568, 40);
      } else if (step != br.countStep) {
        br.countStep = step; ui.dirty = true; sndPlay(880, 25);
      }
      break;
    }
    default: break;
  }
  // A trackball press, judged at the instant the button went down.
  if (prgPressSeq != br.pressSeqSeen) {
    br.pressSeqSeen = prgPressSeq;
    uint32_t at = prgPressMs;
    if (br.phase == BR_INTRO) breachBegin(now);
    else if (br.phase == BR_RUN && (int32_t)(at - br.t0) >= 0) breachPress(at, BR_LAG_MS);
    // (a press during the countdown is too early and does nothing)
  }
  switch (br.phase) {
    case BR_INTRO:
      // Left open and forgotten: back out (nothing has started, nothing is
      // lost) so the screen can sleep.
      if ((int32_t)(now - br.openedAt) > 60000) breachClose(ui.target ? M_DOSSIER : M_NONE);
      break;
    case BR_RUN:
      if ((int32_t)(now - br.t0) >= (int32_t)breachRunLimitMs()) breachJudge(-1, true, 0);
      break;
    case BR_RESULT:
      if ((int32_t)(now - br.resultT) >= (br.hit ? 900 : 1100)) breachLeave();
      break;
    default: break;
  }
}

// A key: SPACE / ENTER play, BACKSPACE backs out (before the run only).
static void breachKey(char c) {
  uint32_t now = millis();
  bool go = (c == ' ' || c == '\r' || c == '\n');
  if (br.phase == BR_INTRO) {
    if (go) breachBegin(now);
    else if (c == 0x08 || c == 0x7F) { breachClose(ui.target ? M_DOSSIER : M_NONE); sfx(SFX_BACK); }
    return;
  }
  if (br.phase == BR_RUN && go) breachPress(now, BR_LAG_MS + BR_KEY_LAG_MS);
  // Backing out once the ball is moving would be a free escape from a
  // bad-looking run, so it is not allowed: the run always resolves.
}

// ── drawing ─────────────────────────────────
static void breachDrawBar(uint32_t now) {
  // Clear the strip the ball lives in.
  GR.fillRect(BR_X0 - 8, BR_Y - 14, BR_LEN + 16, 28, PAL.panel);
  // The line and the gap.
  GR.fillRect(BR_X0, BR_Y - 1, BR_LEN, 2, PAL.line);
  int zx = BR_X0 + br.zoneC - br.zoneW / 2;
  uint16_t zc = PAL.accent;
  if (br.phase == BR_RESULT) zc = br.voided ? PAL.dim : br.hit ? PAL.good : PAL.bad;
  GR.fillRect(zx, BR_Y - 5, br.zoneW, 10, zc);
  int cx = BR_X0 + br.zoneC - br.coreW / 2;
  if (br.coreW > 0) GR.fillRect(cx, BR_Y - 5, br.coreW, 10, avMix(zc, 0xFFFF, 110));
  // End stops.
  GR.fillRect(BR_X0 - 3, BR_Y - 7, 2, 14, PAL.dim);
  GR.fillRect(BR_X0 + BR_LEN + 1, BR_Y - 7, 2, 14, PAL.dim);
  // The ball.
  int pos;
  if (br.phase == BR_RUN)          pos = breachBallPos(breachRunT(now));
  else if (br.phase == BR_RESULT)  pos = br.stopPos >= 0 ? br.stopPos : breachBallPos(breachRunLimitMs());
  else                             pos = br.startRight ? BR_LEN : 0;
  int bx = BR_X0 + pos;
  uint16_t bc = br.phase == BR_RESULT ? (br.voided ? PAL.dim : br.hit ? PAL.good : PAL.bad) : PAL.fg;
  GR.fillCircle(bx, BR_Y, 6, bc);
  GR.drawFastVLine(bx, BR_Y - 5, 11, PAL.bg);      // its centre is what is judged
  GR.drawCircle(bx, BR_Y, 7, PAL.bg);
  // Where you stopped it, marked.
  if (br.phase == BR_RESULT && br.stopPos >= 0) GR.drawFastVLine(bx, BR_Y - 12, 24, bc);
  br.lastBallX = bx;
}

bool breachLive() { return ui.modal == M_BREACHGAME && (br.phase == BR_COUNT || br.phase == BR_RUN); }

// Passes left before the trace completes: one dot per wall-to-wall run.
#define BR_DOTS_Y 156
static int breachPassesUsed(uint32_t now) { return (int)((uint64_t)breachRunT(now) * 2 / br.periodMs); }
static void breachDrawDots(int used) {
  GR.fillRect(100, BR_DOTS_Y - 4, 120, 9, PAL.panel);
  int x0 = 160 - (br.maxSweeps - 1) * 5;
  for (int k = 0; k < br.maxSweeps; k++)
    if (k < br.maxSweeps - used) GR.fillCircle(x0 + k * 10, BR_DOTS_Y, 3, PAL.dim);
    else GR.drawCircle(x0 + k * 10, BR_DOTS_Y, 3, PAL.line);
}

// A known stat as it is; an assumed one marked "~" (the worst it could be).
static String statStr(float v, bool known) { int i = (int)lroundf(v); return known ? String(i) : "~" + String(i); }

static void drawBreachGame(uint32_t now) {
  panel(2, 32, 316, 192, PAL.warn);
  String nm = br.target == TRAINING_ID ? String("TRAINING") : nodeDisplayName(br.target);
  txt(10, 38, "BREACH", PAL.warn, 2);
  txt(90, 42, clip(nm, 20), PAL.fg);
  // The fight, in numbers: each line is one contest, and what it moves.
  const BreachSpec& s0 = br.spec;
  txt(10, 58, "your BRUTE " + String(skillBrute) + " vs their STEALTH " + statStr(s0.dS, s0.knowS), PAL.dim);
  txtR(310, 58, "gap", PAL.dim);
  txt(10, 68, "your STEALTH " + String(skillStealth) + " vs their BRUTE " + statStr(s0.dB, s0.knowB), PAL.dim);
  txtR(310, 68, "gap", PAL.dim);
  txt(10, 78, "their FIREWALL " + statStr(s0.dF, s0.knowF) + " vs your FIREWALL " + String(skillFirewall), PAL.dim);
  txtR(310, 78, "speed", PAL.dim);
  txt(10, 92, "GAP " + String(br.zoneW) + "px", PAL.accent);
  txt(82, 92, "SPEED " + String(br.speed) + "px/s", PAL.accent);
  txt(196, 92, String(br.windowMs) + "ms", PAL.fg);
  txt(238, 92, breachGrade(br.windowMs), breachGradeCol(br.windowMs));
  if (!s0.knowB || !s0.knowS || !s0.knowF)
    txtC(160, 106, "~ not scouted: assumed the worst. Recon to know.", PAL.warn);
  breachDrawBar(now);
  if (br.phase != BR_RESULT) breachDrawDots(br.phase == BR_RUN ? breachPassesUsed(now) : 0);
  String s; uint16_t sc = PAL.fg;
  switch (br.phase) {
    case BR_INTRO:  s = "Press the trackball when you are ready."; break;
    case BR_COUNT:  { int n = 3 - (int)((now - br.countT0) / 300); s = n > 0 ? String(n) : String("GO"); sc = PAL.warn; } break;
    case BR_RUN:    s = "NOW. Stop the ball in the gap."; sc = PAL.warn; break;
    case BR_RESULT: s = br.voided ? "Link lost. Nothing sent." :
                        br.timedOut ? "Too slow. Traced." : br.hit ? (br.clean ? "CLEAN BREACH." : "YOU'RE IN.") : "MISSED. Traced.";
                    sc = br.voided ? PAL.dim : br.hit ? PAL.good : PAL.bad; break;
  }
  if (br.phase == BR_COUNT) txtC(160, 170, s, sc, 3);
  else txtC(160, 176, s, sc, br.phase == BR_RESULT ? 2 : 1);
  // How far off, in time: what you adjust next time.
  if (br.phase == BR_RESULT && !br.timedOut && abs(br.errMs) < 1000) {
    String e = br.errMs == 0 ? String("dead on") :
               String(abs(br.errMs)) + " ms " + (br.errMs > 0 ? "late" : "early");
    txtC(160, 198, e + "   (gap: +/-" + String(br.windowMs / 2) + " ms)", PAL.dim);
  }
  if (br.phase == BR_INTRO) {
    txtC(160, 196, "A miss is a failed hack: XP lost, 30 min lock.", PAL.dim);
    drawHint("press ball / SPACE: start   BKSP back");
  } else if (br.phase == BR_RUN) drawHint("press the TRACKBALL (most precise) or SPACE");
  else drawHint("");
}

// Fast path while the ball moves: only the bar strip is redrawn and pushed,
// every ~10 ms, so the motion is smooth and cheap. (The verdict never depends
// on this — see the note at the top.)
static bool breachAnimate(uint32_t now) {
  if (ui.modal != M_BREACHGAME || br.phase != BR_RUN) return false;
  if ((int32_t)(now - br.lastPush) < 10) return true;
  br.lastPush = now;
  breachDrawBar(now);
  pushRect(BR_X0 - 8, BR_Y - 14, BR_LEN + 16, 28);
  int used = breachPassesUsed(now);
  if (used != br.dotsShown) {
    br.dotsShown = used;
    breachDrawDots(used);
    pushRect(100, BR_DOTS_Y - 4, 120, 9);
  }
  return true;
}

// ── the balance ──────────────────────────────
//
//  One number decides how hard a breach is: D, the time the ball spends
//  inside the gap on each pass. Hit chance for a person is set by how D
//  compares with their own timing spread (~20 ms for a sharp player, ~35 ms
//  typical, ~55 ms a beginner), so D is what the stats move:
//
//    D = D0 · e^( 1.75·(aB−dS)/K )       your BRUTE vs their STEALTH
//           · e^( 1.05·(aS−dB)/K )       your STEALTH vs their BRUTE
//           / e^( 1.40·(dF−aF)/K )       their FIREWALL vs your FIREWALL
//           · (1 + 3% per recon round)
//    V = V0 · e^( 1.40·(dF−aF)/K )       the ball's speed
//    W = D · V                           the gap
//    K = 6 + every point both players own
//
//  Each contest is the DIFFERENCE of two stats measured against all the
//  points in play. Linear inside the exponent, so a point is worth the same
//  wherever you put it: spreading points evenly is not secretly better than
//  specialising, and no build wins everything.
//
//  So your BRUTE widens the gap, their STEALTH narrows it and their FIREWALL
//  makes the ball faster (the gap is untouched by firewall: W = D·V and the
//  firewall term cancels) — and every stat also counts the other way round,
//  with the same total weight (2.8) per point, so no build is attack-only or
//  dead weight.
//
//  SCALABLE BY CONSTRUCTION: only differences relative to K appear, and K grows with
//  the points in play, so a point is worth the same share of a fight at
//  level 32 as at level 3. Two equal builds meet at D0 at every level; a
//  level-27 facing a level-32 is as up against it as a 5 facing a 6; and no
//  way of spending points pulls ahead as levels climb (each build's
//  attack-minus-defence stays within a few points of any other from L5 to
//  L32).
//
//  D0 = 65 ms puts an even fight at ~57% for a typical player who has not
//  scouted (every hidden stat is assumed at its worst) and ~73% with full
//  recon (the old dice gave 60% and 75%). D is held in
//  30..100 ms (≈ 30%..85% typical), so the longest odds are never hopeless
//  and the best never a formality — a sharp player can still beat BRUTAL, a
//  careless one can still miss EASY.
#define BRT_D0       65.0f     // ms
#define BRT_V0       450.0f    // px/s
#define BRT_D_MIN    30.0f
#define BRT_D_MAX    100.0f
#define BRT_V_MIN    240       // px/s: slower is dull and hard to judge
#define BRT_V_MAX    900       // px/s: faster and the eye cannot follow it
#define BRT_W_MIN    10        // px: still clearly visible
#define BRT_W_MAX    (BR_LEN - 2 * BR_EDGE - 16)
#define BRT_LEGS     6         // wall-to-wall runs before the trace completes
#define BRT_PRACTICE 1.60f     // the dummy is a teacher: a kinder gap...
#define BRT_D_MAX_PRACTICE 130.0f   // ...that a beginner can actually learn on

static float brtRatio(float a, float b, float K, float e) {
  if (a < 0) a = 0;
  if (b < 0) b = 0;
  return expf(e * (a - b) / K);
}

BreachTune breachTune(int aB, int aS, int aF, float dB, float dS, float dF, int recon, bool practice) {
  if (recon < 0) recon = 0;
  if (recon > RECON_MAX_SEQ) recon = RECON_MAX_SEQ;
  float K  = 6.0f + (float)(aB + aS + aF) + dB + dS + dF;
  float rV = brtRatio(dF, (float)aF, K, BRT_E_V);
  float D  = BRT_D0 * brtRatio((float)aB, dS, K, BRT_E_W) * brtRatio((float)aS, dB, K, BRT_E_T) / rV;
  D *= 1.0f + 0.03f * (float)recon;
  if (practice) D *= BRT_PRACTICE;
  float dMax = practice ? BRT_D_MAX_PRACTICE : BRT_D_MAX;
  if (D < BRT_D_MIN) D = BRT_D_MIN;
  if (D > dMax) D = dMax;
  int V = (int)lroundf(BRT_V0 * rV);
  if (V < BRT_V_MIN) V = BRT_V_MIN;
  if (V > BRT_V_MAX) V = BRT_V_MAX;
  // The gap has to be visible and fit the bar; if it would not, the speed
  // gives instead, so D — the actual difficulty — is kept.
  int W = (int)lroundf(D * (float)V / 1000.0f);
  if (W < BRT_W_MIN) { V = (int)ceilf(BRT_W_MIN * 1000.0f / D); if (V > BRT_V_MAX) V = BRT_V_MAX; }
  if (W > BRT_W_MAX) { V = (int)(BRT_W_MAX * 1000.0f / D);      if (V < BRT_V_MIN) V = BRT_V_MIN; }
  W = (int)lroundf(D * (float)V / 1000.0f);
  if (W < BRT_W_MIN) W = BRT_W_MIN;
  if (W > BRT_W_MAX) W = BRT_W_MAX;
  BreachTune t;
  t.zoneW    = W;
  t.coreW    = W / 5 < 3 ? 3 : W / 5;
  t.speed    = V;
  t.periodMs = (uint32_t)((2L * BR_LEN * 1000L + V / 2) / V);
  t.maxSweeps = BRT_LEGS;
  t.windowMs = (int)(((long)W * 1000L + V / 2) / V);   // from the integers actually used
  return t;
}
