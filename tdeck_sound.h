#pragma once
// ─────────────────────────────────────────────
//  T-Deck sound: a tiny square-wave synth on the I2S speaker
// ─────────────────────────────────────────────
//
//  The speaker sits behind a MAX98357A I2S amplifier (BCK 7, WS 5, DOUT 6), so
//  tone() cannot drive it — it needs PCM. A low-priority task blocks on a queue
//  of notes and streams square waves into the I2S DMA buffers; with nothing to
//  play it sleeps on the queue and costs nothing.
//
//  Arduino-ESP32 3.x ships ESP_I2S (IDF 5's driver) and aborts at boot if the
//  legacy driver is linked beside it; 2.x only has the legacy one. So: ESP_I2S
//  on 3.x, driver/i2s.h on 2.x. Off target (the host tests) it is all no-ops.

#define SND_BCK   7
#define SND_WS    5
#define SND_DOUT  6
#define SND_RATE  16000

struct SndNote { uint16_t hz, ms; };   // hz 0 = rest

// 0 = mute .. 3 = loud. The amp is loud; full scale would be painful.
uint8_t sndVolume = 2;
static const int16_t SND_AMP[4] = { 0, 900, 2200, 4500 };

#if defined(ARDUINO_ARCH_ESP32)
#include <esp_arduino_version.h>
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  #include <ESP_I2S.h>
  static I2SClass sndI2S;
#else
  #include <driver/i2s.h>
#endif

static QueueHandle_t sndQ = nullptr;

static void sndWrite(const int16_t* b, size_t bytes) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  sndI2S.write((const uint8_t*)b, bytes);
#else
  size_t w; i2s_write(I2S_NUM_0, b, bytes, &w, portMAX_DELAY);
#endif
}

static void sndTask(void*) {
  static int16_t buf[256];
  SndNote n;
  for (;;) {
    xQueueReceive(sndQ, &n, portMAX_DELAY);          // idle = blocked, no CPU
    int16_t amp = SND_AMP[sndVolume & 3];
    uint32_t total = (uint32_t)SND_RATE * n.ms / 1000, left = total, ph = 0;
    uint32_t inc = n.hz ? (uint32_t)(((uint64_t)n.hz << 32) / SND_RATE) : 0;
    const uint32_t RAMP = 64;                         // 4 ms fade in and out: no clicks
    while (left) {
      size_t k = left < 256 ? left : 256;
      for (size_t i = 0; i < k; i++) {
        uint32_t pos = total - left + i, end = total - pos;
        int32_t a = amp;
        if (pos < RAMP) a = a * (int32_t)pos / RAMP;
        if (end < RAMP) a = a * (int32_t)end / RAMP;
        buf[i] = (int16_t)((n.hz && a) ? ((ph & 0x80000000u) ? a : -a) : 0);
        ph += inc;
      }
      sndWrite(buf, k * 2); left -= k;
    }
    if (!uxQueueMessagesWaiting(sndQ)) {              // drain to silence, no hiss
      memset(buf, 0, sizeof buf);
      for (int j = 0; j < 4; j++) sndWrite(buf, sizeof buf);
    }
  }
}

bool sndBegin() {                                     // after GPIO10 is HIGH
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  sndI2S.setPins(SND_BCK, SND_WS, SND_DOUT);
  if (!sndI2S.begin(I2S_MODE_STD, SND_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO))
    return false;
#else
  i2s_config_t c;
  memset(&c, 0, sizeof c);
  c.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  c.sample_rate          = SND_RATE;
  c.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
  c.channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT;
  c.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  c.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
  c.dma_buf_count        = 4;
  c.dma_buf_len          = 256;
  c.tx_desc_auto_clear   = true;
  i2s_pin_config_t p;
  memset(&p, 0, sizeof p);
  p.mck_io_num   = I2S_PIN_NO_CHANGE;
  p.bck_io_num   = SND_BCK;
  p.ws_io_num    = SND_WS;
  p.data_out_num = SND_DOUT;
  p.data_in_num  = I2S_PIN_NO_CHANGE;
  if (i2s_driver_install(I2S_NUM_0, &c, 0, NULL) != ESP_OK) return false;
  i2s_set_pin(I2S_NUM_0, &p);
#endif
  sndQ = xQueueCreate(48, sizeof(SndNote));
  if (!sndQ) return false;
  xTaskCreatePinnedToCore(sndTask, "snd", 4096, NULL, 2, NULL, 0);
  return true;
}
void sndPlay(uint16_t hz, uint16_t ms) {
  if (!sndQ || !sndVolume) return;
  SndNote n{hz, ms};
  xQueueSend(sndQ, &n, 0);                            // never block the game
}
#else
bool sndBegin() { return false; }
uint32_t g_sndNotes = 0;                              // host tests count them
void sndPlay(uint16_t, uint16_t) { g_sndNotes++; }
#endif

void sndSeq(const SndNote* s, int n) { for (int i = 0; i < n; i++) sndPlay(s[i].hz, s[i].ms); }

// ── the game's sounds ────────────────────────
enum Sfx : uint8_t {
  SFX_CLICK, SFX_NAV, SFX_BACK, SFX_ERR, SFX_NEWNODE, SFX_MSG, SFX_WIN, SFX_LOSE,
  SFX_LEVELUP, SFX_ALERT, SFX_HELD, SFX_TILE0, SFX_ROUND, SFX_PERFECT, SFX_SEND,
  SFX_BOOT,
};
// The recon grid's nine tiles, a pentatonic scale so any order sounds fine.
static const uint16_t SND_TILE_HZ[9] = { 523, 587, 659, 784, 880, 1047, 1175, 1319, 1568 };

void sfx(uint8_t s, int arg = 0) {
  switch (s) {
    case SFX_CLICK: sndPlay(1800, 12); break;
    case SFX_NAV:   sndPlay(1200, 10); break;
    case SFX_BACK:  sndPlay(700, 18); break;
    case SFX_ERR:   { static const SndNote q[] = {{220, 90}, {0, 30}, {180, 140}}; sndSeq(q, 3); } break;
    // The portal's new-contact chime: A4, C5, G5.
    case SFX_NEWNODE: { static const SndNote q[] = {{440, 80}, {0, 15}, {523, 90}, {0, 20}, {784, 140}}; sndSeq(q, 5); } break;
    case SFX_MSG:   { static const SndNote q[] = {{988, 70}, {0, 30}, {1319, 120}}; sndSeq(q, 3); } break;
    case SFX_SEND:  { static const SndNote q[] = {{660, 40}, {990, 60}}; sndSeq(q, 2); } break;
    case SFX_WIN:   { static const SndNote q[] = {{523, 70}, {659, 70}, {784, 70}, {1047, 200}}; sndSeq(q, 4); } break;
    case SFX_LOSE:  { static const SndNote q[] = {{392, 120}, {330, 120}, {262, 120}, {196, 260}}; sndSeq(q, 4); } break;
    case SFX_HELD:  { static const SndNote q[] = {{300, 50}, {0, 30}, {300, 50}, {600, 160}}; sndSeq(q, 4); } break;
    case SFX_LEVELUP: { static const SndNote q[] = {{523, 90}, {659, 90}, {784, 90}, {1047, 90},
                                                   {0, 40}, {784, 90}, {1047, 300}}; sndSeq(q, 7); } break;
    case SFX_ALERT: { static const SndNote q[] = {{880, 90}, {660, 90}, {880, 90}, {660, 90}}; sndSeq(q, 4); } break;
    case SFX_TILE0: sndPlay(arg ? SND_TILE_HZ[arg % 9] : 1175, 70); break;
    case SFX_ROUND: { static const SndNote q[] = {{1047, 50}, {1568, 90}}; sndSeq(q, 2); } break;
    case SFX_PERFECT: { static const SndNote q[] = {{1047, 80}, {1319, 80}, {1568, 80}, {2093, 80},
                                                   {1568, 80}, {2093, 300}}; sndSeq(q, 6); } break;
    case SFX_BOOT:  { static const SndNote q[] = {{262, 60}, {392, 60}, {523, 120}}; sndSeq(q, 3); } break;
  }
}
