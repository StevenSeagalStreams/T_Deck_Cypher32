#pragma once
// ─────────────────────────────────────────────
//  WORLD BOARD — the online scoreboard (T-Deck)
// ─────────────────────────────────────────────
//
//  The game stays offline-first: nothing here is needed to play, and the
//  radio protocol is untouched. When the player has given it a Wi-Fi network
//  and switched it on, the T-Deck now and then joins that network for a few
//  seconds, tells the board what happened in its fights, and brings back its
//  world rank and the top ten.
//
//  OPPONENT-CONFIRMED. A device only ever reports its own side of a hack:
//  "I breached X", "I was traced by X", "X breached me", "I held X off",
//  each tagged with the radio sequence number of that hack's request (both
//  devices know it). The board counts a fight only when both sides have
//  reported it — so a modified device cannot score on its own. The rules
//  live in the database (supabase/ in this repository): points, the weekly
//  cap between the same two players, the game's own lock times.
//
//  WHAT IS SENT: the chip id, codename, faction, level and XP, and the fight
//  reports above. Nothing else — no location, no Wi-Fi details, no messages.
//
//  THREADING: the sync runs in its own FreeRTOS task on core 0, so joining
//  Wi-Fi and a TLS handshake (a few seconds) never freeze the screen, the
//  breach game or the radio on core 1. The loop hands it a snapshot of the
//  player; everything the two share is behind netLock.
//
//  Included from tdeck_app.h.

#if defined(ARDUINO_ARCH_ESP32)
  #include <WiFiClientSecure.h>
  #include <HTTPClient.h>
  #include <time.h>
  #include "tdeck_certs.h"
#endif

// The board. The publishable key is public by design: it only lets a caller
// run the three functions the database exposes (register, sync, board),
// each of which checks what it is given.
#define C32_BOARD_HOST "rxpodxloxbybkbystqzd.supabase.co"
#define C32_BOARD_KEY  "sb_publishable_ADPGpjk7sfQtalWVQE3Ejw_niOaUZAj"
#define C32_BOARD_PAGE "stevenseagalstreams.github.io/T_Deck_Cypher32/board.html"

#define NET_Q            24                  // fight reports waiting to go up
#define NET_PERIOD_MS    (15UL * 60000UL)    // routine sync
#define NET_AFTER_FIGHT  (60UL * 1000UL)     // a fight gets reported soon after
#define NET_RETRY_MS     (5UL * 60000UL)     // after a failure
#define NET_FIRST_MS     (20UL * 1000UL)     // after boot

enum { NS_OFF, NS_IDLE, NS_WIFI, NS_SYNC, NS_OK, NS_FAIL };

struct NetFight {
  uint32_t eid;       // ours; a resend is not a second report
  uint32_t other;     // their chip id
  uint32_t atMs;      // millis() when it happened (this boot)
  uint32_t epoch;     // wall clock when it happened, 0 if the clock was unset
  char     kind;      // 'W' 'L' 'B' 'H' (see loraFightHook)
  uint8_t  seq;       // the hack request's radio sequence number
};

struct NetRow { int r; char n[17]; char f; int l; int s; };

struct Net {
  // settings
  bool     on = false;
  String   ssid, pass, secret;
  bool     registered = false;       // the board has accepted our secret
  uint32_t nextEid = 0;
  // fights waiting to be reported
  NetFight q[NET_Q]; int qn = 0;
  // what the last sync brought back
  volatile uint8_t state = NS_OFF;
  String   err;
  uint32_t lastOkMs = 0, lastTryMs = 0, dueMs = 0;
  bool     everOk = false;
  int      rank = 0, players = 0, score = 0, breaches = 0, holds = 0;
  NetRow   top[10]; int topN = 0;
  // the player, as the loop last saw them (the task never reads game state)
  String   snapName, snapFac; uint32_t snapChip = 0; int snapLevel = 1, snapXp = 0;
  // set by the task when it has something new to show
  volatile bool changed = false;
} net;

// ── locking and the worker ──────────────────
#if defined(ARDUINO_ARCH_ESP32)
static SemaphoreHandle_t netMu = nullptr;
static TaskHandle_t      netTaskH = nullptr;
static void netLock()   { if (netMu) xSemaphoreTake(netMu, portMAX_DELAY); }
static void netUnlock() { if (netMu) xSemaphoreGive(netMu); }
#else
static void netLock()   {}
static void netUnlock() {}
#endif

// ── persistence (its own namespace: a character wipe leaves the board
//    identity alone, so a reset player keeps their place and history) ──
static Preferences netPrefs;
// Both threads save (the loop: settings and new fights; the worker: the
// board secret and acknowledged fights), so every save holds netLock.
static void netSaveSettings() {
  netLock();
  netPrefs.begin("c32-net", false);
  netPrefs.putUChar("on", net.on ? 1 : 0);
  netPrefs.putString("ssid", net.ssid);
  netPrefs.putString("pass", net.pass);
  netPrefs.putString("secret", net.secret);
  netPrefs.putUChar("reg", net.registered ? 1 : 0);
  netPrefs.end();
  netUnlock();
}
// The queue is one string ("eid,other,epoch,kind,seq;..."), one write.
static void netSaveQueue() {
  netLock();
  String v;
  for (int i = 0; i < net.qn; i++) {
    const NetFight& f = net.q[i];
    char e[48];
    snprintf(e, sizeof e, "%lu,%lu,%lu,%c,%u;", (unsigned long)f.eid, (unsigned long)f.other,
             (unsigned long)f.epoch, f.kind, f.seq);
    v += e;
  }
  netPrefs.begin("c32-net", false);
  netPrefs.putULong("eid", net.nextEid);
  netPrefs.putString("q", v);
  netPrefs.end();
  netUnlock();
}
static void netLoad() {
  netPrefs.begin("c32-net", true);
  net.on      = netPrefs.getUChar("on", 0) != 0;
  net.ssid    = netPrefs.getString("ssid", "");
  net.pass    = netPrefs.getString("pass", "");
  net.secret  = netPrefs.getString("secret", "");
  net.registered = netPrefs.getUChar("reg", 0) != 0;
  net.nextEid = netPrefs.getULong("eid", 0);
  String v    = netPrefs.getString("q", "");
  netPrefs.end();
  net.qn = 0;
  int i = 0;
  while (i < (int)v.length() && net.qn < NET_Q) {
    int j = v.indexOf(';', i);
    if (j < 0) break;
    unsigned long eid = 0, other = 0, epoch = 0; char kind = 0; unsigned seq = 0;
    if (sscanf(v.substring(i, j).c_str(), "%lu,%lu,%lu,%c,%u", &eid, &other, &epoch, &kind, &seq) == 5) {
      NetFight& f = net.q[net.qn++];
      f.eid = eid; f.other = other; f.epoch = epoch; f.kind = kind; f.seq = (uint8_t)seq;
      f.atMs = millis();              // this boot: only the wall clock (if any) is still true
    }
    i = j + 1;
  }
  // A fresh device starts its report ids somewhere random, so ids never
  // repeat even if its storage is ever erased.
  if (!net.nextEid) net.nextEid = (uint32_t)random(1, 0x3FFFFFFF);
}

static uint32_t netEpochNow() {
#if defined(ARDUINO_ARCH_ESP32)
  time_t t = time(nullptr);
  return t > 1700000000 ? (uint32_t)t : 0;
#else
  return 0;
#endif
}

// ── the loop side ───────────────────────────
bool netConfigured() { return net.ssid.length() > 0; }

// Called by the radio/game code (loraFightHook) once per hack we take part in.
void netRecordFight(char kind, uint32_t other, uint8_t seq) {
  netLock();
  if (net.qn == NET_Q) { memmove(&net.q[0], &net.q[1], sizeof(NetFight) * (NET_Q - 1)); net.qn--; }
  NetFight& f = net.q[net.qn++];
  f.eid = net.nextEid++; f.other = other; f.seq = seq; f.kind = kind;
  f.atMs = millis(); f.epoch = netEpochNow();
  netUnlock();
  netSaveQueue();
  // Report it soon, while the other side's report is fresh too.
  if (net.on && netConfigured()) {
    uint32_t soon = millis() + NET_AFTER_FIGHT;
    if (!net.dueMs || (int32_t)(net.dueMs - soon) > 0) net.dueMs = soon;
  }
}

static void netDoSync();

// Take a snapshot of the player and wake the worker.
void netRequest(uint32_t chip, const String& name, const String& faction, int level, int xp) {
  if (net.state == NS_WIFI || net.state == NS_SYNC) return;     // one at a time
  netLock();
  net.snapChip = chip; net.snapName = name; net.snapFac = faction;
  net.snapLevel = level; net.snapXp = xp;
  net.state = NS_WIFI; net.err = "";
  net.lastTryMs = millis();
  netUnlock();
#if defined(ARDUINO_ARCH_ESP32)
  if (netTaskH) xTaskNotifyGive(netTaskH);
#else
  netDoSync();                         // host tests: synchronously
#endif
}

// Whether a routine sync is due now (the app calls netRequest when it is,
// and when nothing more important — a breach run — is going on).
bool netDue(uint32_t now) {
  if (!net.on || !netConfigured()) return false;
  if (net.state == NS_WIFI || net.state == NS_SYNC) return false;
  return net.dueMs && (int32_t)(now - net.dueMs) >= 0;
}

// ── transport: the real one on the device, a fake one in the host tests ──
// join Wi-Fi / POST to a database function / leave Wi-Fi
typedef bool (*NetJoinFn)(const String& ssid, const String& pass, String& err);
typedef int  (*NetPostFn)(const char* fn, const String& body, String& resp);
typedef void (*NetLeaveFn)();

#if defined(ARDUINO_ARCH_ESP32)
static bool netJoinReal(const String& ssid, const String& pass, String& err) {
  // Keep the phone portal's access point up if it is on.
  WiFi.mode(tdp.portal ? WIFI_AP_STA : WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.length() ? pass.c_str() : nullptr);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) vTaskDelay(pdMS_TO_TICKS(100));
  if (WiFi.status() != WL_CONNECTED) { err = "Can't join " + ssid; return false; }
  // A clock, so certificate dates and fight times are real.
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  t0 = millis();
  while (!netEpochNow() && millis() - t0 < 5000) vTaskDelay(pdMS_TO_TICKS(100));
  return true;
}
static int netPostReal(const char* fn, const String& body, String& resp) {
  WiFiClientSecure tls;
  tls.setCACert(TDECK_ROOT_CAS);
  HTTPClient http;
  http.setTimeout(12000);
  String url = String("https://") + C32_BOARD_HOST + "/rest/v1/rpc/" + fn;
  if (!http.begin(tls, url)) { resp = "no connection"; return -1; }
  http.addHeader("apikey", C32_BOARD_KEY);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(body);
  resp = code > 0 ? http.getString() : http.errorToString(code);
  http.end();
  return code;
}
static void netLeaveReal() {
  WiFi.disconnect(true);
  if (!tdp.portal) WiFi.mode(WIFI_OFF);
}
NetJoinFn  netJoin  = netJoinReal;
NetPostFn  netPost  = netPostReal;
NetLeaveFn netLeave = netLeaveReal;
#else
static bool netJoinNone(const String&, const String&, String& err) { err = "offline"; return false; }
static int  netPostNone(const char*, const String&, String& resp) { resp = "offline"; return -1; }
static void netLeaveNone() {}
NetJoinFn  netJoin  = netJoinNone;
NetPostFn  netPost  = netPostNone;
NetLeaveFn netLeave = netLeaveNone;
#endif

// ── tiny JSON helpers (the replies are small and ours; no library needed) ──
static int jKey(const String& s, const char* key, int from, int to) {
  String k = String("\"") + key + "\":";
  int i = s.indexOf(k, from);
  if (i < 0 || (to >= 0 && i >= to)) return -1;
  i += k.length();
  while (i < (int)s.length() && s[i] == ' ') i++;
  return i;
}
static long jNum(const String& s, const char* key, long def, int from = 0, int to = -1) {
  int i = jKey(s, key, from, to);
  if (i < 0 || strncmp(s.c_str() + i, "null", 4) == 0) return def;
  return strtol(s.c_str() + i, nullptr, 10);
}
static String jStr(const String& s, const char* key, int from = 0, int to = -1) {
  int i = jKey(s, key, from, to);
  if (i < 0 || s[i] != '"') return "";
  String out;
  for (int k = i + 1; k < (int)s.length() && s[k] != '"'; k++) {
    if (s[k] == '\\' && k + 1 < (int)s.length()) k++;
    out += s[k];
  }
  return out;
}
static bool jTrue(const String& s, const char* key) {
  int i = jKey(s, key, 0, -1);
  return i >= 0 && strncmp(s.c_str() + i, "true", 4) == 0;
}
static String jEsc(const String& v) {
  String o;
  for (unsigned i = 0; i < v.length(); i++) {
    char c = v[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c >= 0x20 && (uint8_t)c < 0x7F) o += c;
  }
  return o;
}
// Why a request failed, in words: the board's own reason ("error"), or
// PostgREST's ("message"), or the HTTP/transport code.
static String netWhy(int code, const String& resp) {
  String e = jStr(resp, "error");
  if (!e.length()) e = jStr(resp, "message");
  if (e == "chip already registered") return "Board: this chip id is taken. Ask the board admin.";
  if (e.length()) return "Board: " + e;
  return code < 0 ? "No internet through this network" : "Board unreachable (HTTP " + String(code) + ")";
}
static const char* netKindName(char k) {
  return k == 'W' ? "won" : k == 'L' ? "lost" : k == 'B' ? "breached" : "held";
}

// ── the sync itself (worker task) ───────────
static void netDoSync() {
  netLock();
  String ssid = net.ssid, pass = net.pass, secret = net.secret;
  String name = net.snapName, fac = net.snapFac;
  uint32_t chip = net.snapChip; int level = net.snapLevel, xp = net.snapXp;
  NetFight q[NET_Q]; int qn = net.qn;
  memcpy(q, net.q, sizeof(NetFight) * qn);
  netUnlock();

  auto fail = [&](const String& why) {
    netLock(); net.err = why; net.state = NS_FAIL; net.dueMs = millis() + NET_RETRY_MS; net.changed = true; netUnlock();
    netLeave();
  };

  String err;
  if (!netJoin(ssid, pass, err)) { fail(err); return; }
  net.state = NS_SYNC;
  char chipHex[9]; snprintf(chipHex, sizeof chipHex, "%08lx", (unsigned long)chip);
  String facLetter = fac.length() ? fac.substring(0, 1) : String("?");

  // Claim this chip id on the board, once. The secret is made HERE and kept
  // before it is sent, so a reply lost on the way back costs nothing: the
  // next try presents the same secret, which the board accepts again.
  if (!secret.length()) {
    char s[49];
    for (int i = 0; i < 24; i++) snprintf(s + i * 2, 3, "%02x", (unsigned)(random(0, 256)));
    secret = s;
    netLock(); net.secret = secret; net.registered = false; netUnlock();
    netSaveSettings();
  }
  if (!net.registered) {
    String body = String("{\"p_chip\":\"") + chipHex + "\",\"p_codename\":\"" + jEsc(name) +
                  "\",\"p_faction\":\"" + facLetter + "\",\"p_secret\":\"" + secret + "\"}";
    String resp;
    int code = netPost("c32_register", body, resp);
    if (code != 200 || !jTrue(resp, "ok")) { fail(netWhy(code, resp)); return; }
    netLock(); net.registered = true; netUnlock();
    netSaveSettings();
  }

  uint32_t nowMs = millis(), nowEp = netEpochNow();
  String ev = "[";
  for (int i = 0; i < qn; i++) {
    const NetFight& f = q[i];
    uint32_t age = (f.epoch && nowEp && nowEp >= f.epoch) ? nowEp - f.epoch : (nowMs - f.atMs) / 1000;
    char e[140];
    snprintf(e, sizeof e, "%s{\"eid\":%lu,\"kind\":\"%s\",\"other\":\"%08lx\",\"seq\":%u,\"age\":%lu}",
             i ? "," : "", (unsigned long)f.eid, netKindName(f.kind), (unsigned long)f.other,
             f.seq, (unsigned long)age);
    ev += e;
  }
  ev += "]";
  String body = String("{\"p_chip\":\"") + chipHex + "\",\"p_secret\":\"" + jEsc(secret) +
                "\",\"p_codename\":\"" + jEsc(name) + "\",\"p_faction\":\"" + facLetter +
                "\",\"p_level\":" + String(level) + ",\"p_xp\":" + String(xp) +
                ",\"p_events\":" + ev + "}";
  String resp;
  int code = netPost("c32_sync", body, resp);
  netLeave();
  if (code != 200 || !jTrue(resp, "ok")) { fail(netWhy(code, resp)); return; }

  // Parse what came back, then drop the reports the board acknowledged.
  NetRow top[10]; int topN = 0;
  // (Postgres writes its JSON with a space after every colon and comma.)
  int t = jKey(resp, "top", 0, -1);
  if (t >= 0 && resp[t] == '[') {
    int i = t + 1;
    while (topN < 10) {
      int a = resp.indexOf('{', i), b = a < 0 ? -1 : resp.indexOf('}', a);
      if (a < 0 || b < 0) break;
      NetRow& r = top[topN++];
      r.r = (int)jNum(resp, "r", 0, a, b);
      String n = jStr(resp, "n", a, b); strncpy(r.n, n.c_str(), 16); r.n[16] = 0;
      String f = jStr(resp, "f", a, b); r.f = f.length() ? f[0] : '?';
      r.l = (int)jNum(resp, "l", 1, a, b);
      r.s = (int)jNum(resp, "s", 0, a, b);
      i = b + 1;
    }
  }
  String acked = "";
  int a = jKey(resp, "acked", 0, -1);
  if (a >= 0 && resp[a] == '[') { int b = resp.indexOf(']', a); acked = "," + resp.substring(a + 1, b) + ","; acked.replace(" ", ""); }

  netLock();
  int keep = 0;
  for (int i = 0; i < net.qn; i++) {
    String id = "," + String((unsigned long)net.q[i].eid) + ",";
    if (acked.indexOf(id) < 0) net.q[keep++] = net.q[i];
  }
  net.qn = keep;
  net.rank = (int)jNum(resp, "rank", 0);
  net.players = (int)jNum(resp, "players", 0);
  net.score = (int)jNum(resp, "score", 0);
  net.breaches = (int)jNum(resp, "breaches", 0);
  net.holds = (int)jNum(resp, "holds", 0);
  memcpy(net.top, top, sizeof(NetRow) * topN); net.topN = topN;
  net.lastOkMs = millis(); net.everOk = true;
  net.state = NS_OK; net.dueMs = millis() + NET_PERIOD_MS; net.changed = true;
  netUnlock();
  netSaveQueue();
}

#if defined(ARDUINO_ARCH_ESP32)
static void netTask(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    netDoSync();
  }
}
#endif

void netBegin() {
  netLoad();
  net.state = net.on && netConfigured() ? NS_IDLE : NS_OFF;
  if (net.on && netConfigured()) net.dueMs = millis() + NET_FIRST_MS;
  loraFightHook = netRecordFight;
#if defined(ARDUINO_ARCH_ESP32)
  netMu = xSemaphoreCreateMutex();
  // Core 0 (with the Wi-Fi stack), below the loop's priority; TLS wants a
  // generous stack.
  xTaskCreatePinnedToCore(netTask, "c32net", 12288, nullptr, 1, &netTaskH, 0);
#endif
}

// ── Wi-Fi network list for the setup screen ──
#if defined(ARDUINO_ARCH_ESP32)
static bool netScanStart() {
  if (net.state == NS_WIFI || net.state == NS_SYNC) return false;
  WiFi.mode(tdp.portal ? WIFI_AP_STA : WIFI_STA);
  WiFi.scanNetworks(true);
  return true;
}
static int    netScanDone()        { int n = WiFi.scanComplete(); return n == WIFI_SCAN_RUNNING ? -1 : (n < 0 ? 0 : n); }
static String netScanSsid(int i)   { return WiFi.SSID(i); }
static int    netScanRssi(int i)   { return WiFi.RSSI(i); }
static bool   netScanOpen(int i)   { return WiFi.encryptionType(i) == WIFI_AUTH_OPEN; }
static void   netScanEnd()         { WiFi.scanDelete(); if (!tdp.portal && net.state != NS_WIFI && net.state != NS_SYNC) WiFi.mode(WIFI_OFF); }
#else
// Host tests: a fixed list.
static const char* NET_FAKE_SSIDS[] = { "HomeNet", "CoffeeShop", "Neighbour-5G" };
static bool   netScanStart()       { return true; }
static int    netScanDone()        { return 3; }
static String netScanSsid(int i)   { return NET_FAKE_SSIDS[i]; }
static int    netScanRssi(int i)   { return -50 - i * 10; }
static bool   netScanOpen(int i)   { return i == 1; }
static void   netScanEnd()         {}
#endif
