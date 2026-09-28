#pragma once
// ─────────────────────────────────────────────
//  The living avatar
// ─────────────────────────────────────────────
//
//  The hooded hacker from the e-ink screens, rebuilt for a colour panel and
//  given a pulse. Drawn procedurally every frame into an off-screen canvas and
//  pushed as one block, so it animates without flicker and costs ~7 ms of SPI
//  per frame at the size it is drawn.
//
//  Three layers decide what you see:
//    mood     cyMood -4..+4 from the game — posture, eye shape, eye colour,
//             how busy the hands are, what it says
//    action   a short behaviour it is doing right now — typing, looking
//             round, sipping coffee, stretching, dozing
//    reaction an event that interrupts everything — a new node, a hack won
//             or lost, a level up, a message, being scouted or breached
//
//  Nothing here touches game state. The app feeds it mood and events; it only
//  draws.
#include <Adafruit_GFX.h>

#define AV_W 136
#define AV_H 128

enum AvReaction : uint8_t {
  AV_R_NONE = 0,
  AV_R_WIN,        // hack landed / firewall held
  AV_R_LOSE,       // hack failed / breached
  AV_R_LEVELUP,
  AV_R_NEWNODE,    // someone appeared on the radar
  AV_R_MESSAGE,    // mail in
  AV_R_ALERT,      // someone is hacking or scouting us
  AV_R_SCAN,       // we are probing someone
};

enum AvAction : uint8_t {
  AV_A_TYPE = 0,   // default: hands on the keys
  AV_A_LOOK,       // glances left and right
  AV_A_COFFEE,     // raises a mug
  AV_A_STRETCH,    // arms up, yawns
  AV_A_THINK,      // still, eyes up, a "..." thought
  AV_A_SLEEP,      // head down, zzz
  AV_A_STARE,      // looks straight at you. unsettling.
  AV_A_GLITCH,     // flickers out of phase for a moment
};

// Particles: binary rain, sparks, zzz, hearts of smoke. Kept tiny.
struct AvParticle { int16_t x, y; int8_t vx, vy; uint8_t life, kind; uint16_t col; };
#define AV_PARTICLES 18
enum { AVP_BIT0, AVP_BIT1, AVP_SPARK, AVP_Z, AVP_SMOKE, AVP_EXCL };

static inline uint16_t avRgb(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
static inline uint16_t avMix(uint16_t a, uint16_t b, int t /*0..255*/) {
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  return (uint16_t)(((ar + (br - ar) * t / 255) << 11) |
                    ((ag + (bg - ag) * t / 255) << 5) |
                     (ab + (bb - ab) * t / 255));
}

// Cheap deterministic noise for the avatar's own randomness, so it never
// perturbs the game's random() stream (which seeds beacon jitter and rolls).
static uint32_t avRngState = 0x1234567u;
static inline uint32_t avRand() {
  avRngState ^= avRngState << 13; avRngState ^= avRngState >> 17; avRngState ^= avRngState << 5;
  return avRngState;
}
static inline int avRange(int lo, int hi) { return lo + (int)(avRand() % (uint32_t)(hi - lo + 1)); }

// Integer sine, 0..1023 phase -> -256..256. Avoids float in the frame loop.
static inline int avSin(uint32_t ph) {
  static const int16_t q[17] = { 0, 25, 50, 74, 98, 121, 142, 162, 181, 198, 213,
                                 226, 237, 245, 251, 255, 256 };
  ph &= 1023;
  int quad = ph >> 8, i = (ph & 255) >> 4, f = ph & 15;
  int a, b;
  if (quad == 0 || quad == 2) { a = q[i]; b = q[i + 1]; }
  else                        { a = q[16 - i]; b = q[15 - i < 0 ? 0 : 15 - i]; }
  int v = a + (b - a) * f / 16;
  return quad < 2 ? v : -v;
}

class Avatar {
public:
  GFXcanvas16* cv = nullptr;
  // Inputs from the game.
  int      mood    = 0;          // -4..+4
  uint16_t accent  = 0x07E0;     // faction colour
  uint16_t bg      = 0x0000;     // what it is drawn over
  bool     night   = false;      // long idle: dim and sleepy
  bool     cold    = false;      // battery nearly flat: shivers
  // What it is doing.
  AvAction   action = AV_A_TYPE;
  AvReaction react  = AV_R_NONE;
  uint32_t   actionUntil = 0, reactStart = 0, reactUntil = 0;
  uint32_t   blinkAt = 0, blinkUntil = 0;
  int8_t     lookX = 0, lookY = 0, lookTX = 0, lookTY = 0;
  uint32_t   lookNext = 0;
  AvParticle p[AV_PARTICLES];
  uint32_t   lastStep = 0, lastEmit = 0;
  uint32_t   frame = 0;

  bool begin() {
    if (!cv) cv = new GFXcanvas16(AV_W, AV_H);
    if (!cv || !cv->getBuffer()) return false;
    for (auto& q : p) q.life = 0;
    avRngState ^= (uint32_t)(uintptr_t)cv;
    return true;
  }

  // An event. Overrides whatever it was doing for `ms`.
  void trigger(AvReaction r, uint32_t now, uint32_t ms = 3500) {
    react = r; reactStart = now; reactUntil = now + ms;
    action = AV_A_TYPE; actionUntil = now + ms;
    lookTX = 0; lookTY = 0;
    switch (r) {
      case AV_R_WIN: case AV_R_LEVELUP:
        for (int i = 0; i < 10; i++) emit(AVP_SPARK, avRange(20, AV_W - 20), avRange(10, 70),
                                          avRange(-2, 2), avRange(-3, -1), avRange(14, 30));
        break;
      case AV_R_LOSE:
        for (int i = 0; i < 6; i++) emit(AVP_SMOKE, avRange(50, 86), 88, avRange(-1, 1), -1, avRange(20, 40));
        break;
      case AV_R_NEWNODE: lookTX = 3; break;
      case AV_R_ALERT:   emit(AVP_EXCL, AV_W / 2 + 22, 6, 0, 0, 30); break;
      default: break;
    }
  }
  bool reacting(uint32_t now) const { return react != AV_R_NONE && (int32_t)(reactUntil - now) > 0; }

  // Advance the simulation. Call before draw().
  void step(uint32_t now) {
    if (react != AV_R_NONE && (int32_t)(now - reactUntil) >= 0) react = AV_R_NONE;

    // Pick the next idle behaviour. Mood decides the menu: a happy hacker
    // types and drinks coffee, a tilted one stares and sulks, a bored one
    // dozes off.
    if (!reacting(now) && (int32_t)(now - actionUntil) >= 0) {
      int r = avRange(0, 99);
      if (night || (mood <= -3 && r < 25)) action = AV_A_SLEEP;
      else if (r < 45) action = AV_A_TYPE;
      else if (r < 62) action = AV_A_LOOK;
      else if (r < 75) action = AV_A_COFFEE;
      else if (r < 82) action = (mood >= 0) ? AV_A_STRETCH : AV_A_THINK;
      else if (r < 89) action = AV_A_STARE;
      else if (r < 93) action = AV_A_GLITCH;
      else             action = (mood <= -1) ? AV_A_SLEEP : AV_A_THINK;
      actionUntil = now + (action == AV_A_SLEEP ? avRange(6000, 12000)
                          : action == AV_A_TYPE ? avRange(4000, 9000)
                          : action == AV_A_GLITCH ? avRange(500, 900)
                          :                       avRange(2500, 4000));
    }

    // Eyes wander on their own schedule.
    if ((int32_t)(now - lookNext) >= 0) {
      lookNext = now + avRange(700, 2600);
      if (action == AV_A_LOOK) { lookTX = (lookTX > 0) ? -3 : 3; lookTY = 0; }
      else if (action == AV_A_THINK) { lookTX = avRange(-1, 1); lookTY = -2; }
      else if (action == AV_A_TYPE) { lookTX = avRange(-1, 1); lookTY = 2; }
      else if (action == AV_A_STARE) { lookTX = 0; lookTY = -1; }
      else { lookTX = avRange(-2, 2); lookTY = avRange(-1, 1); }
      if (react == AV_R_NEWNODE) { lookTX = 3; lookTY = 0; }
    }
    if (lookX < lookTX) lookX++; else if (lookX > lookTX) lookX--;
    if (lookY < lookTY) lookY++; else if (lookY > lookTY) lookY--;

    // Blink: every 2-6 s, sometimes a double blink.
    if ((int32_t)(now - blinkAt) >= 0) {
      blinkUntil = now + 110;
      blinkAt = now + (avRange(0, 5) == 0 ? 220 : avRange(2000, 6000));
    }

    // Ambient particles.
    if ((uint32_t)(now - lastEmit) > 260) {
      lastEmit = now;
      if (action == AV_A_SLEEP && !reacting(now) && avRange(0, 3) == 0)
        emit(AVP_Z, AV_W / 2 + 18, 30, 1, -1, 40);
      else if (action == AV_A_TYPE && !reacting(now) && mood >= 0 && avRange(0, 2) == 0)
        emit(avRange(0, 1) ? AVP_BIT1 : AVP_BIT0, avRange(46, 90), 84, 0, -1, avRange(18, 30));
      if (react == AV_R_LEVELUP && avRange(0, 1) == 0)
        emit(AVP_SPARK, avRange(10, AV_W - 10), AV_H - 10, avRange(-1, 1), -3, 30);
      if (react == AV_R_LOSE && avRange(0, 1) == 0)
        emit(AVP_SMOKE, avRange(56, 80), 88, 0, -1, 30);
    }
    // Move particles at a fixed rate regardless of frame rate.
    while ((uint32_t)(now - lastStep) >= 50) {
      lastStep += 50;
      if ((uint32_t)(now - lastStep) > 1000) lastStep = now;
      for (auto& q : p) if (q.life) {
        q.x += q.vx; q.y += q.vy; q.life--;
        if (q.kind == AVP_SPARK && (q.life & 3) == 0) q.vy++;          // gravity
        if (q.kind == AVP_Z && (q.life & 7) == 0) q.vx = -q.vx;        // drift
      }
    }
    frame++;
  }

  void draw(uint32_t now);

private:
  void emit(uint8_t kind, int x, int y, int vx, int vy, int life) {
    for (auto& q : p) if (!q.life) {
      q.x = x; q.y = y; q.vx = vx; q.vy = vy; q.life = life; q.kind = kind;
      q.col = kind == AVP_SMOKE ? avRgb(90, 90, 90)
            : kind == AVP_Z     ? avRgb(150, 170, 255)
            : kind == AVP_EXCL  ? avRgb(255, 60, 60)
            : kind == AVP_SPARK ? (avRange(0, 1) ? avRgb(255, 230, 90) : accent)
            : accent;
      return;
    }
  }
  void drawEye(int cx, int cy, int dir, uint32_t now, uint16_t glow);
};

// Faction colours, shared with the rest of the UI.
static inline uint16_t factionColour(char f) {
  switch (f) {
    case 'B': return avRgb(170, 110, 255);   // BLACK: violet glow on black
    case 'W': return avRgb(230, 240, 255);
    case 'R': return avRgb(255, 70, 70);
    case 'G': return avRgb(60, 255, 130);
    default:  return avRgb(120, 200, 255);
  }
}

// One eye: a soft glow with a bright core, shaped by mood and reaction.
// dir is -1 for the left eye, +1 for the right (angry/sad brows slant).
void Avatar::drawEye(int cx, int cy, int dir, uint32_t now, uint16_t glow) {
  GFXcanvas16& c = *cv;
  bool blink = (int32_t)(blinkUntil - now) > 0;
  uint16_t halo = avMix(0x0000, glow, 70), mid = avMix(0x0000, glow, 150);
  uint16_t core = avMix(glow, 0xFFFF, 110);

  AvReaction r = reacting(now) ? react : AV_R_NONE;
  if (action == AV_A_SLEEP && r == AV_R_NONE) {             // closed: - -
    c.drawFastHLine(cx - 4, cy + 1, 9, mid);
    return;
  }
  if (r == AV_R_LOSE && ((now / 90) & 1)) {                   // glitching X
    c.drawLine(cx - 3, cy - 3, cx + 3, cy + 3, glow);
    c.drawLine(cx - 3, cy + 3, cx + 3, cy - 3, glow);
    return;
  }
  if (r == AV_R_WIN || r == AV_R_LEVELUP || (mood >= 3 && r == AV_R_NONE && !blink)) {
    // Happy arcs: ^ ^
    c.fillCircle(cx, cy, 6, halo);
    c.drawLine(cx - 4, cy + 1, cx, cy - 3, core);
    c.drawLine(cx, cy - 3, cx + 4, cy + 1, core);
    c.drawLine(cx - 4, cy + 2, cx, cy - 2, mid);
    c.drawLine(cx, cy - 2, cx + 4, cy + 2, mid);
    return;
  }
  if (blink) { c.drawFastHLine(cx - 4, cy, 9, mid); return; }

  int rx = 3, ry = 3;
  if (r == AV_R_ALERT || r == AV_R_NEWNODE) { rx = 4; ry = 5; }     // wide open
  if (mood <= -3 && r == AV_R_NONE) ry = 2;                         // heavy lids
  c.fillCircle(cx, cy, rx + 4, halo);
  c.fillRoundRect(cx - rx - 1, cy - ry - 1, 2 * rx + 3, 2 * ry + 3, 3, mid);
  c.fillRoundRect(cx - rx + 1, cy - ry + 1, 2 * rx - 1, 2 * ry - 1, 2, core);
  // Brows cut into the glow: angry slants down to the middle, sad up.
  if (mood <= -2 || r == AV_R_ALERT) {
    int in = (r == AV_R_ALERT || mood <= -3) ? 1 : -1;       // 1 angry, -1 sad
    int x0 = cx - 6, x1 = cx + 6;
    int yIn = cy - ry - 1 + (in > 0 ? 2 : -1), yOut = cy - ry - 1 + (in > 0 ? -2 : 2);
    if (dir < 0) c.fillTriangle(x0, yOut - 3, x1, yIn - 3, x1, yIn, bg == 0 ? 0x0000 : bg),
                 c.drawLine(x0, yOut, x1, yIn, 0x0000);
    else         c.fillTriangle(x0, yIn - 3, x1, yOut - 3, x0, yIn, 0x0000),
                 c.drawLine(x0, yIn, x1, yOut, 0x0000);
  }
}

void Avatar::draw(uint32_t now) {
  if (!cv || !cv->getBuffer()) return;
  GFXcanvas16& c = *cv;
  AvReaction r = reacting(now) ? react : AV_R_NONE;
  uint32_t age = r ? now - reactStart : 0;

  // Palette.
  uint16_t hood    = avRgb(34, 38, 48);
  uint16_t hoodHi  = avRgb(58, 64, 80);
  uint16_t hoodLo  = avRgb(20, 22, 28);
  uint16_t face    = avRgb(6, 6, 10);
  uint16_t skin    = avRgb(120, 96, 84);
  uint16_t glow    = accent;
  if (mood <= -3) glow = avMix(accent, avRgb(255, 60, 40), 150);      // tilted: red
  if (r == AV_R_LOSE)  glow = avRgb(255, 50, 50);
  if (r == AV_R_ALERT) glow = ((now / 150) & 1) ? avRgb(255, 60, 60) : accent;
  if (night && r == AV_R_NONE) glow = avMix(0x0000, glow, 140);

  // Posture: breathing, slump when down, bounce when celebrating.
  int breath = avSin(now * 1024 / 3600) * 3 / 256;              // ±3 px, 3.6 s
  int slump  = mood <= -2 ? 4 : mood <= -1 ? 2 : 0;
  if (action == AV_A_SLEEP && r == AV_R_NONE) slump = 7;
  int bounce = 0;
  if (r == AV_R_WIN || r == AV_R_LEVELUP) bounce = -abs(avSin(age * 1024 / 500) * 8 / 256);
  if (r == AV_R_LOSE) slump = 6;
  int shake = (r == AV_R_ALERT || (r == AV_R_LOSE && age < 800)) ? ((now / 40) & 1 ? 2 : -2) : 0;
  if (cold && r == AV_R_NONE && (now % 20000) < 600) shake = (now / 50) & 1 ? 1 : -1;
  int headY = 44 + breath / 2 + slump + bounce;
  int cx    = AV_W / 2 + shake;
  int headTilt = (action == AV_A_LOOK && r == AV_R_NONE) ? lookX / 2 : 0;

  // Background: faint scanlines and a glow from the laptop.
  c.fillScreen(bg);
  for (int y = (frame & 1); y < AV_H; y += 4) c.drawFastHLine(0, y, AV_W, avMix(bg, glow, 10));
  if (r == AV_R_LEVELUP) {
    int rad = 20 + (age / 18) % 60;
    c.drawCircle(cx, headY + 10, rad, avMix(bg, glow, 120));
    c.drawCircle(cx, headY + 10, (rad + 30) % 80, avMix(bg, glow, 60));
  }
  if (r == AV_R_SCAN || r == AV_R_NEWNODE) {
    for (int k = 0; k < 3; k++) {
      int rad = (int)((age / 12 + k * 25) % 75);
      c.drawCircle(cx, headY, rad + 10, avMix(bg, glow, 140 - rad * 2 > 0 ? 140 - rad * 2 : 0));
    }
  }

  // Body: shoulders to the bottom edge, rising and falling with the breath.
  int sh = 76 + breath / 3 + slump / 2 + bounce / 2;
  c.fillTriangle(cx - 46, AV_H, cx + 46, AV_H, cx, sh - 10, hood);
  c.fillRoundRect(cx - 44, sh, 88, AV_H - sh + 8, 18, hood);
  c.drawFastVLine(cx, sh + 6, AV_H - sh, hoodLo);                     // zip
  c.drawLine(cx - 44, sh + 16, cx - 30, sh + 2, hoodHi);               // shoulder rim
  c.drawLine(cx + 44, sh + 16, cx + 30, sh + 2, hoodLo);

  // Hood: a rounded head with a peak, the face a void inside it, and the
  // cloth falling from it onto the shoulders so head and body are one piece.
  int hx = cx + headTilt;
  c.fillTriangle(hx - 27, headY + 6, cx - 40, sh + 10, cx - 6, sh + 4, hood);
  c.fillTriangle(hx + 27, headY + 6, cx + 40, sh + 10, cx + 6, sh + 4, hood);
  c.drawLine(hx - 27, headY + 6, cx - 40, sh + 10, hoodHi);
  c.fillTriangle(hx - 3, headY - 38, hx - 28, headY - 4, hx + 24, headY - 8, hood);
  c.fillCircle(hx, headY, 28, hood);
  c.drawCircle(hx, headY, 28, hoodHi);
  c.fillCircle(hx + 2, headY + 1, 27, hood);                           // soften the rim
  c.drawLine(hx - 3, headY - 38, hx - 26, headY - 8, hoodHi);          // rim light
  c.fillRoundRect(hx - 18, headY - 14, 36, 34, 14, face);
  c.fillCircle(hx, headY + 4, 17, face);
  // Laptop glow lights the chin.
  int lidGlow = 40 + (int)((avSin(now * 1024 / 900) + 256) / 16);
  if (action == AV_A_SLEEP && r == AV_R_NONE) lidGlow = 20;
  c.drawFastHLine(hx - 12, headY + 19, 24, avMix(face, glow, lidGlow));
  c.drawFastHLine(hx - 9,  headY + 20, 18, avMix(face, glow, lidGlow / 2));

  // Eyes.
  int ex = lookX, ey = lookY;
  if (action == AV_A_SLEEP && r == AV_R_NONE) ey = 3;
  drawEye(hx - 8 + ex, headY + 2 + ey, -1, now, glow);
  drawEye(hx + 8 + ex, headY + 2 + ey, +1, now, glow);
  // Mouth, only when it has something to say about it.
  if (r == AV_R_WIN || r == AV_R_LEVELUP || mood >= 3) {
    c.drawLine(hx - 4, headY + 12, hx, headY + 14, avMix(face, glow, 120));
    c.drawLine(hx, headY + 14, hx + 4, headY + 12, avMix(face, glow, 120));
  } else if (r == AV_R_LOSE || mood <= -3) {
    c.drawLine(hx - 4, headY + 14, hx, headY + 12, avMix(face, glow, 100));
    c.drawLine(hx, headY + 12, hx + 4, headY + 14, avMix(face, glow, 100));
  } else if (action == AV_A_STRETCH || (r == AV_R_ALERT)) {
    c.fillCircle(hx, headY + 13, 2, avMix(face, glow, 80));             // yawn / gasp
  }

  // Arms and the laptop.
  int lidTop = 92, lidBot = 118;
  bool lidClosedABit = (action == AV_A_SLEEP && r == AV_R_NONE);
  if (r == AV_R_WIN || r == AV_R_LEVELUP || action == AV_A_STRETCH) {
    // Arms up.
    int wave = avSin(now * 1024 / 400) * 3 / 256;
    c.fillRoundRect(cx - 50, sh - 30 + wave, 10, 40, 5, hood);
    c.fillRoundRect(cx + 40, sh - 30 - wave, 10, 40, 5, hood);
    c.fillCircle(cx - 45, sh - 32 + wave, 5, skin);
    c.fillCircle(cx + 45, sh - 32 - wave, 5, skin);
  }
  // Laptop: we see the back of the lid, logo glowing in the faction colour.
  int lh = lidClosedABit ? 18 : 26;
  c.fillRoundRect(cx - 32, lidBot - lh, 64, lh, 3, avRgb(70, 74, 86));
  c.drawRoundRect(cx - 32, lidBot - lh, 64, lh, 3, avRgb(110, 116, 130));
  c.fillRect(cx - 38, lidBot, 76, 5, avRgb(90, 94, 108));
  c.drawFastHLine(cx - 38, lidBot + 5, 76, avRgb(40, 42, 50));
  int ly = lidBot - lh / 2;
  uint16_t logo = avMix(avRgb(70, 74, 86), glow, lidGlow + 80 > 255 ? 255 : lidGlow + 80);
  // Logo: a skull-ish glyph: circle with two eye dots.
  c.fillCircle(cx, ly - 1, 5, logo);
  c.fillRect(cx - 3, ly + 3, 7, 3, logo);
  c.drawPixel(cx - 2, ly - 1, avRgb(70, 74, 86));
  c.drawPixel(cx + 2, ly - 1, avRgb(70, 74, 86));
  (void)lidTop;

  // Hands on the keys unless they are busy elsewhere.
  if (!(r == AV_R_WIN || r == AV_R_LEVELUP || action == AV_A_STRETCH)) {
    bool typing = (action == AV_A_TYPE && r == AV_R_NONE) || r == AV_R_SCAN;
    int speed = mood >= 2 ? 70 : mood <= -2 ? 220 : 120;
    int tl = typing && ((now / speed) & 1) ? -2 : 0;
    int tr = typing && ((now / speed + 1) & 1) ? -2 : 0;
    int handY = lidBot - 1;
    if (action == AV_A_SLEEP && r == AV_R_NONE) { tl = tr = 1; }
    c.fillRoundRect(cx - 44, handY - 8 + tl, 12, 10, 4, skin);
    if (action == AV_A_COFFEE && r == AV_R_NONE) {
      // The mug comes up to the face and goes back down.
      uint32_t ph = (now % 3000);
      int lift = ph < 1500 ? (int)(ph / 40) : (int)((3000 - ph) / 40);
      if (lift > 30) lift = 30;
      int mx = cx + 30, my = handY - 12 - lift;
      c.fillRoundRect(mx, my, 12, 13, 2, avRgb(200, 200, 210));
      c.drawCircle(mx + 13, my + 6, 3, avRgb(200, 200, 210));
      c.fillRoundRect(mx - 2, my + 6, 10, 9, 4, skin);
      if ((now / 300) % 3 == 0) c.drawPixel(mx + 4, my - 3, avRgb(160, 160, 160)),
                                c.drawPixel(mx + 6, my - 5, avRgb(140, 140, 140));
    } else {
      c.fillRoundRect(cx + 32, handY - 8 + tr, 12, 10, 4, skin);
    }
  }

  // Particles.
  for (auto& q : p) if (q.life) {
    uint16_t col = q.life < 8 ? avMix(bg, q.col, q.life * 32) : q.col;
    switch (q.kind) {
      case AVP_BIT0: case AVP_BIT1:
        c.setTextSize(1); c.setTextColor(col);
        c.setCursor(q.x, q.y); c.print(q.kind == AVP_BIT1 ? '1' : '0'); break;
      case AVP_SPARK:
        c.drawPixel(q.x, q.y, col); c.drawPixel(q.x + 1, q.y, col);
        c.drawPixel(q.x, q.y + 1, col); break;
      case AVP_Z:
        c.setTextSize(q.life > 20 ? 1 : 2); c.setTextColor(col);
        c.setCursor(q.x, q.y); c.print('z'); break;
      case AVP_SMOKE:
        c.fillCircle(q.x, q.y, 2 + (40 - q.life) / 10, col); break;
      case AVP_EXCL:
        c.fillRect(q.x, q.y, 4, 12, col); c.fillRect(q.x, q.y + 15, 4, 4, col); break;
    }
  }

  // Loss: a glitch band tears across the frame for the first second.
  if (r == AV_R_LOSE && age < 1200 && (avRand() & 3) == 0) {
    int y = avRange(0, AV_H - 6), h = avRange(2, 6), off = avRange(-6, 6);
    uint16_t* buf = c.getBuffer();
    for (int j = y; j < y + h; j++) {
      uint16_t row[AV_W];
      memcpy(row, buf + j * AV_W, sizeof row);
      for (int i = 0; i < AV_W; i++) {
        int s = i - off; if (s < 0) s = 0; if (s >= AV_W) s = AV_W - 1;
        buf[j * AV_W + i] = row[s] ^ ((j & 1) ? 0xF800 : 0);
      }
    }
  }
  // A self-glitch: the whole figure slips sideways in bands for a moment.
  if (action == AV_A_GLITCH && r == AV_R_NONE && (avRand() & 1)) {
    uint16_t* buf = c.getBuffer();
    for (int b = 0; b < 3; b++) {
      int y = avRange(8, AV_H - 10), h = avRange(2, 5), off = avRange(-5, 5);
      for (int j = y; j < y + h && j < AV_H; j++) {
        uint16_t row[AV_W];
        memcpy(row, buf + j * AV_W, sizeof row);
        for (int i = 0; i < AV_W; i++) {
          int s = i - off; if (s < 0) s = 0; if (s >= AV_W) s = AV_W - 1;
          buf[j * AV_W + i] = (b == 1) ? avMix(row[s], accent, 90) : row[s];
        }
      }
    }
  }
  // Thinking: dots over the head.
  if (action == AV_A_THINK && r == AV_R_NONE) {
    int n = (now / 400) % 4;
    for (int k = 0; k < n; k++) c.fillCircle(cx + 22 + k * 7, headY - 34 - k * 3, 2, avMix(bg, glow, 170));
  }
}
