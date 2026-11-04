// Per-controller GIP (Game Input Protocol) state machine, modelled on xone.
// Everything here runs on the main protocol task.
#include "gip.h"
#include "pairing.h"

#define CHUNK_MAX 640

// GIP command ids
#define C_ACK 0x01
#define C_ANNOUNCE 0x02
#define C_STATUS 0x03
#define C_IDENTIFY 0x04
#define C_POWER 0x05
#define C_VKEY 0x07
#define C_RUMBLE 0x09
#define C_LED 0x0a
#define C_INPUT 0x20
// option bits
#define O_ACKREQ 0x10
#define O_INTERNAL 0x20
#define O_CHUNK_START 0x40
#define O_CHUNK 0x80

struct Priv {
  uint8_t seq;
  uint8_t chunk[CHUNK_MAX];
  uint32_t chunkTotal;
  uint8_t chunkCmd;
  bool chunkActive, identStarted, identDone, powered;
  uint8_t identTries, lastAnySeq;
  bool haveAnySeq;
  uint32_t lastIdent, lastAssocResp, lastRumble, identSince, lastRestart;
  volatile bool dropReq;
  volatile bool rumbleDirty, ledDirty;
  uint8_t rumble[4];
  uint8_t ledMode, ledBright;
};

static Pad pads[MAX_PADS];
static Priv priv[MAX_PADS];
static PadListener listener;
static volatile bool dbg = false;
void gip_set_debug(bool on) { dbg = on; }

extern void gipLogRx(int, uint8_t, uint8_t, uint8_t, uint32_t, uint32_t);
extern void gipLogTx(int, uint8_t, uint8_t, uint8_t, int, bool);
#define Serial0Log gipLogRx
#define TxLog gipLogTx

static int findPad(const uint8_t *mac) {
  for (int i = 0; i < MAX_PADS; i++)
    if (pads[i].stage != PAD_FREE && !memcmp(pads[i].mac, mac, 6)) return i;
  return -1;
}

static int allocSlot(const uint8_t *mac) {
  int k = pairing_index(mac);
  if (k >= 0 && k < MAX_PADS && pads[k].stage == PAD_FREE) return k;
  for (int i = 0; i < MAX_PADS; i++) if (pads[i].stage == PAD_FREE) return i;
  return -1;
}

static uint8_t nextSeq(int i) {
  uint8_t s = priv[i].seq++;
  if (!priv[i].seq) priv[i].seq = 1;
  return s ? s : 1;
}

static bool sendGip(int i, uint8_t cmd, uint8_t opt, const uint8_t *pl, int plen, int seq = 0) {
  uint8_t b[4 + 32];
  if (plen > 32) return false;
  b[0] = cmd; b[1] = opt; b[2] = seq ? seq : nextSeq(i); b[3] = plen;
  if (plen) memcpy(b + 4, pl, plen);
  bool ok = radio_send_qos(pads[i].mac, b, 4 + plen);
  if (dbg) TxLog(i, cmd, opt, b[2], plen, ok);
  return ok;
}

static void sendAck(int i, uint8_t cmd, uint8_t seq, uint16_t len, uint16_t rem) {
  uint8_t pl[9] = {0, cmd, O_INTERNAL, (uint8_t)len, (uint8_t)(len >> 8), 0, 0, (uint8_t)rem, (uint8_t)(rem >> 8)};
  sendGip(i, C_ACK, O_INTERNAL, pl, 9, seq);
}

static void sendIdentify(int i) { sendGip(i, C_IDENTIFY, O_INTERNAL, nullptr, 0); }
static void sendPowerOn(int i) { uint8_t pl[1] = {0x00}; sendGip(i, C_POWER, O_INTERNAL, pl, 1); }
static void sendLed(int i, uint8_t mode, uint8_t br) { uint8_t pl[3] = {0, mode, br}; sendGip(i, C_LED, O_INTERNAL, pl, 3); }
static void sendRumble(int i, const uint8_t *r) {
  // {unknown, motors mask, LT, RT, left, right, duration, delay, repeat}
  uint8_t pl[9] = {0, 0x0f, r[2], r[3], r[0], r[1], 0xff, 0x00, 0xeb};
  sendGip(i, C_RUMBLE, 0x00, pl, 9);
}

static void flushPending(int i);
static void notify(int i, PadEvent ev) { if (listener) listener(i, ev); }

static void becomeReady(int i) {
  Pad &p = pads[i];
  if (p.stage == PAD_READY) return;
  p.stage = PAD_READY;
  if (!priv[i].powered) { sendPowerOn(i); priv[i].powered = true; }
  { uint8_t stop[4] = {0, 0, 0, 0}; sendRumble(i, stop); }   // like xone: clear any rumble state
  sendLed(i, 1, priv[i].ledBright ? priv[i].ledBright : 20);
  notify(i, PADEV_READY);
}

static void dropPad(int i) {
  Pad &p = pads[i];
  if (p.stage == PAD_FREE) return;
  memset(&p.in, 0, sizeof(p.in));
  p.stage = PAD_FREE;
  radio_register(i, nullptr);
  notify(i, PADEV_DISCONNECTED);
}

static void resetPriv(int i) {
  uint8_t keepBr = priv[i].ledBright;
  memset(&priv[i], 0, sizeof(Priv));
  priv[i].seq = 1;
  priv[i].ledBright = keepBr;
}

static uint16_t u16(const uint8_t *p) { return p[0] | (p[1] << 8); }

static int gipVarint(const uint8_t *p, int n, uint32_t *out) {
  uint32_t v = 0; int i = 0;
  for (; i < 4 && i < n; i++) { v |= (uint32_t)(p[i] & 0x7f) << (7 * i); if (!(p[i] & 0x80)) { i++; break; } }
  *out = v; return i;
}

static void onInput(int i, const uint8_t *pl, uint32_t plen) {
  Pad &p = pads[i];
  if (plen < 14) return;
  p.in.buttons = u16(pl);
  p.in.lt = u16(pl + 2);
  p.in.rt = u16(pl + 4);
  p.in.lx = (int16_t)u16(pl + 6);
  p.in.ly = (int16_t)~u16(pl + 8);   // GIP Y is up-positive; HID wants up-negative
  p.in.rx = (int16_t)u16(pl + 10);
  p.in.ry = (int16_t)~u16(pl + 12);
  if (plen == 44) p.in.share = pl[18] & 1;  // Series controllers: share byte at len-26
  p.inputCount++;
  p.lastInputAt = millis();
  if (p.stage != PAD_READY) becomeReady(i);   // reconnect without re-announce
}

static void handlePacket(int i, uint8_t cmd, uint8_t opt, uint8_t seq, uint32_t plen, uint32_t coff, const uint8_t *pl) {
  Pad &p = pads[i];
  Priv &v = priv[i];
  if (!(opt & O_CHUNK)) {          // the controller numbers all its packets with one counter
    if (v.haveAnySeq && seq != v.lastAnySeq) {
      uint8_t exp = v.lastAnySeq + 1; if (!exp) exp = 1;
      uint8_t gap = (uint8_t)(seq - exp);
      if (gap && gap < 64) p.inputGaps += gap;
    }
    v.lastAnySeq = seq; v.haveAnySeq = true;
  }
  if (dbg && cmd != C_INPUT) Serial0Log(i, cmd, opt, seq, plen, coff);
  if (opt & O_CHUNK_START) { v.chunkTotal = coff; coff = 0; v.chunkCmd = cmd; v.chunkActive = v.chunkTotal <= CHUNK_MAX; }
  if (opt & O_CHUNK) {
    if (!v.chunkActive || cmd != v.chunkCmd) return;
    if (plen) {
      uint32_t endo = coff + plen;
      if (endo <= CHUNK_MAX) memcpy(v.chunk + coff, pl, plen);
      if ((opt & O_ACKREQ) || endo == v.chunkTotal) sendAck(i, cmd, seq, endo, v.chunkTotal - endo);
    } else {
      v.chunkActive = false;       // empty chunk = transfer complete
      if (cmd == C_IDENTIFY) {
        v.identDone = true; p.identified = true;
        becomeReady(i);
        sendPowerOn(i);                                   // xone order: identify -> power on -> LED ...
        sendLed(i, 1, v.ledBright ? v.ledBright : 20);
      }
    }
    return;
  }
  if (opt & O_ACKREQ) sendAck(i, cmd, seq, plen, 0);
  switch (cmd) {
    case C_ANNOUNCE:
      if (plen >= 12) { p.vid = u16(pl + 8); p.pid = u16(pl + 10); }
      // The controller (re)started its GIP session (e.g. it was power-cycled while we stayed associated):
      // forget what we thought we knew and run the handshake again.
      if ((p.stage == PAD_READY || v.identDone) && millis() - v.lastRestart > 3000) {
        v.lastRestart = millis();
        v.identStarted = v.identDone = v.powered = v.chunkActive = false;
        v.identTries = 0; v.haveAnySeq = false;
        p.identified = false;
        p.stage = PAD_IDENT; v.identSince = millis();
      }
      if (p.stage < PAD_IDENT) { p.stage = PAD_IDENT; v.identSince = millis(); }
      if (!v.identStarted && millis() - v.lastIdent > 500) { v.lastIdent = millis(); sendIdentify(i); }
      break;
    case C_STATUS:
      if (plen >= 1) { p.battRaw = pl[0]; p.battType = (pl[0] >> 2) & 3; p.battLevel = pl[0] & 3; p.battKnown = true; }
      break;
    case C_VKEY:
      if (plen >= 2 && pl[1] == 0x5b) {
        bool down = pl[0] != 0;
        p.in.guide = down;
        notify(i, down ? PADEV_GUIDE_DOWN : PADEV_GUIDE_UP);
      }
      break;
    case C_INPUT:
      onInput(i, pl, plen);
      break;
    case C_IDENTIFY: v.identStarted = true; break;
    default: break;
  }
}

static void handleGipFrame(int i, const uint8_t *g, int gl) {
  while (gl > 4) {
    uint8_t cmd = g[0], opt = g[1], seq = g[2];
    uint32_t plen = 0, coff = 0;
    int h = 3 + gipVarint(g + 3, gl - 3, &plen);
    if (opt & O_CHUNK) h += gipVarint(g + h, gl - h, &coff);
    if ((int)(h + plen) > gl) break;
    if (cmd == C_IDENTIFY) priv[i].identStarted = true;
    handlePacket(i, cmd, opt, seq, plen, coff, g + h);
    g += h + plen;
    gl -= h + plen;
  }
}

static void sendAssocResp(int i) {
  static const uint8_t body[14] = {0x00, 0x00, 0x10, 0x01, 0x00, 0x0f, 0, 0, 0, 0, 0, 0, 0, 0};
  radio_send_mgmt(0x10, pads[i].mac, body, sizeof(body));
}

static void flushPending(int i) {
  Priv &v = priv[i];
  uint32_t now = millis();
  if (pads[i].stage != PAD_READY) return;
  if (v.rumbleDirty && now - v.lastRumble >= 15) { v.rumbleDirty = false; v.lastRumble = now; sendRumble(i, v.rumble); }
  if (v.ledDirty) { v.ledDirty = false; sendLed(i, v.ledMode, v.ledBright); }
  if (v.rumbleDirty || v.ledDirty) radio_set_pending_tx(i, true);
}

void gip_begin(PadListener l) { listener = l; for (int i = 0; i < MAX_PADS; i++) priv[i].seq = 1; }

void gip_handle(const RadioEvent &ev) {
  if (ev.kind == EV_ASSOC) {
    if (!pairing_allows(ev.mac)) return;
    int i = findPad(ev.mac);
    bool fresh = false;
    if (i < 0) {
      i = allocSlot(ev.mac);
      if (i < 0) return;
      fresh = true;
    }
    Pad &p = pads[i];
    pairing_add(ev.mac);
    if (fresh) {
      uint32_t assocs = 0;
      memset(&p, 0, sizeof(p));
      memcpy(p.mac, ev.mac, 6);
      p.connectedAt = millis();
      resetPriv(i);
      p.assocCount = assocs;
      radio_register(i, ev.mac);
      p.stage = PAD_ASSOC;
      notify(i, PADEV_CONNECTED);
    } else if (p.stage != PAD_READY) {
      resetPriv(i);
      p.stage = PAD_ASSOC;
    }
    p.assocCount++;
    p.rssi = ev.rssi;
    p.channel = ev.channel;
    if (millis() - priv[i].lastAssocResp > 40) { priv[i].lastAssocResp = millis(); sendAssocResp(i); }
  } else if (ev.kind == EV_PAIRREQ) {
    if (!pairing_active()) return;
    static const uint8_t body[11] = {0x70, 0x02, 0x00, 0x45, 0x55, 0x01, 0x0f, 0x8f, 0xff, 0x87, 0x1f};
    if (radio_send_mgmt(0x70, ev.mac, body, sizeof(body))) {
      pairing_add(ev.mac);
      pairing_paired_one();
    }
  } else if (ev.kind == EV_NULL) {
    int i = findPad(ev.mac);
    if (i >= 0) flushPending(i);
  } else if (ev.kind == EV_GIP) {
    int i = findPad(ev.mac);
    if (i < 0) return;
    pads[i].rssi = ev.rssi;
    handleGipFrame(i, ev.data, ev.len);
    flushPending(i);
  }
}

void gip_tick() {
  uint32_t now = millis();
  for (int i = 0; i < MAX_PADS; i++) {
    Pad &p = pads[i];
    if (p.stage == PAD_FREE) continue;
    if (now - radio_last_rx(i) > PAD_TIMEOUT_MS) { dropPad(i); continue; }
    Priv &v = priv[i];
    if (v.dropReq) { v.dropReq = false; dropPad(i); continue; }
    // Identification is optional (input flows without it): don't let a stalled handshake block the pad.
    if (p.stage == PAD_IDENT && now - v.identSince > 1500) becomeReady(i);
    if (!v.identDone && v.identTries < 6 && now - v.lastIdent > 1200 && now - p.connectedAt > 800) { v.lastIdent = now; v.identTries++; sendIdentify(i); }
    if (p.stage == PAD_READY) {
      if ((v.rumbleDirty || v.ledDirty) && now - v.lastRumble >= 150) flushPending(i);   // fallback if no frame arrived
    }
  }
}

const Pad &gip_pad(int slot) { return pads[slot]; }
int gip_connected_count() { int n = 0; for (int i = 0; i < MAX_PADS; i++) if (pads[i].stage == PAD_READY) n++; return n; }

void gip_rumble(int slot, uint8_t l, uint8_t r, uint8_t lt, uint8_t rt) {
  if (slot < 0 || slot >= MAX_PADS) return;
  priv[slot].rumble[0] = l > 100 ? 100 : l; priv[slot].rumble[1] = r > 100 ? 100 : r;
  priv[slot].rumble[2] = lt > 100 ? 100 : lt; priv[slot].rumble[3] = rt > 100 ? 100 : rt;
  priv[slot].rumbleDirty = true;
  radio_set_pending_tx(slot, true);
}

void gip_led(int slot, uint8_t mode, uint8_t brightness) {
  if (slot < 0 || slot >= MAX_PADS) return;
  priv[slot].ledMode = mode; priv[slot].ledBright = brightness; priv[slot].ledDirty = true; radio_set_pending_tx(slot, true);
}

void gip_disconnect(int slot) { if (slot >= 0 && slot < MAX_PADS) priv[slot].dropReq = true; }

// Diagnostics: send a rumble/LED packet with the ACK-request flag set and see whether the controller acknowledges it.
void gip_test_ack(int slot, int what) {
  if (slot < 0 || slot >= MAX_PADS || pads[slot].stage == PAD_FREE) return;
  if (what == 0) {
    uint8_t pl[9] = {0, 0x0f, 0, 0, 80, 80, 0xff, 0x00, 0xeb};
    sendGip(slot, C_RUMBLE, O_ACKREQ, pl, 9);
  } else if (what == 1) {
    uint8_t pl[9] = {0, 0x0f, 0, 0, 80, 80, 0xff, 0x00, 0xeb};
    sendGip(slot, C_RUMBLE, O_INTERNAL | O_ACKREQ, pl, 9);
  } else {
    uint8_t pl[3] = {0, 2, 20};
    sendGip(slot, C_LED, O_INTERNAL | O_ACKREQ, pl, 3);
  }
}
