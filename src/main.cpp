#include <Arduino.h>
#include "config.h"
#include "radio.h"
#include "gip.h"
#include "pairing.h"
#include "hid.h"
#include "bootguard.h"
#if HAS_SCREEN
#include "ui.h"
#endif
#include "leds.h"
#include "knob.h"
#if HAS_PMIC
#include "power.h"
#include "powerbtn.h"
#endif
#include "esp_task_wdt.h"

static uint32_t restartAt = 0;

static void onPad(int slot, PadEvent ev) {
  const Pad &p = gip_pad(slot);
  const char *n[] = {"CONNECTED", "READY", "DISCONNECTED", "GUIDE_DOWN", "GUIDE_UP", "INPUT"};
  USBSerial.printf("[pad %d] %s %02x:%02x:%02x:%02x:%02x:%02x vid=%04x pid=%04x\n", slot, n[ev],
                p.mac[0], p.mac[1], p.mac[2], p.mac[3], p.mac[4], p.mac[5], p.vid, p.pid);
  leds_pad_event(slot, ev);
  // Like a real Xbox: the Xbox button (or a controller powering on) wakes the computer.
  if (ev == PADEV_GUIDE_DOWN || ev == PADEV_READY) {
    if (hid_wake_host()) { leds_flash(140, 140, 140, 600); USBSerial.println("USB remote wakeup sent"); }
  }
  // More controllers than USB gamepads exposed: expose more (needs a USB re-enumeration = restart).
  if (ev == PADEV_READY && slot >= hid_slots() && hid_slots() < hid_max_slots() && !restartAt) {
    if (slot + 1 > pairing_count()) hid_request_slots(slot + 1);   // normally the paired count already covers it
    restartAt = millis() + 2500;
    leds_flash(170, 0, 255, 2500);
    USBSerial.println("new controller needs more USB gamepads: restarting to expose them");
  }
}

void gipLogRx(int i, uint8_t cmd, uint8_t opt, uint8_t seq, uint32_t plen, uint32_t coff) {
  USBSerial.printf("  [pad%d RX] cmd=%02x opt=%02x seq=%02x plen=%lu coff=%lu\n", i, cmd, opt, seq, (unsigned long)plen, (unsigned long)coff);
}
void gipLogTx(int i, uint8_t cmd, uint8_t opt, uint8_t seq, int plen, bool ok) {
  USBSerial.printf("  [pad%d TX] cmd=%02x opt=%02x seq=%02x plen=%d %s\n", i, cmd, opt, seq, plen, ok ? "ok" : "FAILED");
}

static void status() {
  static uint32_t lastIn[MAX_PADS];
  const RadioStats &s = radio_stats();
#if HAS_PMIC
  { char pb[96]; power_report(pb, sizeof(pb)); USBSerial.printf("power: %s\n", pb); }
#endif
  USBSerial.printf("last reset: %s (crash count %lu, last loop step before it: %u)\n", boot_reason(), (unsigned long)boot_crash_count(), boot_prev_mark());
  USBSerial.printf("radio: tx power %d (%.1f dBm), filter=%d\n", radio_tx_power(), radio_tx_power() / 4.0f, radio_filter());
  USBSerial.printf("usb mounted=%d suspended=%d slots=%d reports=%lu rumbleMsgs=%lu\n", hid_mounted(), hid_suspended(), hid_slots(), (unsigned long)hid_reports_sent(), (unsigned long)hid_rumble_msgs());
  USBSerial.printf("== ch%u %s pairing=%d known=%d | rx=%lu toMe=%lu dup=%lu qdrop=%lu txerr=%lu\n", radio_channel(),
                radio_scanning() ? "scan" : "hold", pairing_active(), pairing_count(), (unsigned long)s.rxFrames,
                (unsigned long)s.toMe, (unsigned long)s.dupDropped, (unsigned long)s.queueDrops, (unsigned long)s.txErr);
  for (int i = 0; i < MAX_PADS; i++) {
    const Pad &p = gip_pad(i);
    if (p.stage == PAD_FREE) continue;
    USBSerial.printf("  pad%d stage=%d rssi=%d in=%lu (+%lu/10s) assoc=%lu id=%d batt=%d/%d(raw %02x) up=%lus gip(seen=%lu missed=%lu) lostFrames=%lu/%lu\n", i, p.stage, p.rssi,
                  (unsigned long)p.inputCount, (unsigned long)(p.inputCount - lastIn[i]), (unsigned long)p.assocCount, p.identified,
                  p.battType, p.battLevel, p.battRaw, (unsigned long)((millis() - p.connectedAt) / 1000), (unsigned long)p.gipSeen, (unsigned long)p.inputGaps, (unsigned long)radio_lost_frames(i), (unsigned long)radio_rx_frames(i));
    { char sb[120]; radio_seq_report(i, sb, sizeof(sb));
      USBSerial.printf("    %s\n    input gaps ms: <10:%lu <20:%lu <40:%lu <80:%lu <160:%lu >=160:%lu\n", sb,
                    (unsigned long)p.inHist[0], (unsigned long)p.inHist[1], (unsigned long)p.inHist[2], (unsigned long)p.inHist[3], (unsigned long)p.inHist[4], (unsigned long)p.inHist[5]); }
    lastIn[i] = p.inputCount;
  }
}

void setup() {
  bootguard_begin();
#if HAS_PMIC
  powerbtn_begin();
#endif
  esp_task_wdt_init(5, true);   // panic + reset if the main loop stalls for 5 s
  esp_task_wdt_add(NULL);
  pairing_begin();
  hid_begin();                                   // USB first, so a power hiccup can't starve enumeration
  uint32_t t0 = millis();
  while (!hid_mounted() && millis() - t0 < 3000) delay(10);   // let the computer enumerate us
  delay(300);
#if HAS_PMIC
  power_begin();
#endif
  radio_begin();
  delay(200);
  leds_begin();
#if HAS_SCREEN
  delay(200);
  ui_begin();
#endif
#ifdef AUTO_PAIR_BOOT_MS
  pairing_start(AUTO_PAIR_BOOT_MS);   // headless: pairing window opens at every power-up
#endif
  gip_begin(onPad);
  USBSerial.printf("XBOX BRIDGE ready, known controllers: %d\n", pairing_count());
  USBSerial.println("keys: s=status B=bootloader R=restart p=pairing f=forget-all v=rumble l=led d=debug");
}

static uint32_t rumbleOff = 0;

void loop() {
  esp_task_wdt_reset();
  boot_mark(1);
  RadioEvent ev;
  for (int n = 0; n < 24 && radio_poll(ev); n++) gip_handle(ev);
  boot_mark(2); gip_tick();
  boot_mark(3); pairing_tick();
  radio_set_pairing(pairing_beacon_flag());
  boot_mark(4); radio_tick();
  boot_mark(5); hid_task();
  boot_mark(6);
  if (rumbleOff && (int32_t)(millis() - rumbleOff) >= 0) { rumbleOff = 0; for (int i = 0; i < MAX_PADS; i++) gip_rumble(i, 0, 0, 0, 0); USBSerial.println("rumble off"); }
  bootguard_tick();
  if (restartAt && (int32_t)(millis() - restartAt) >= 0) ESP.restart();
  boot_mark(7); knob_tick();
#if HAS_PMIC
  powerbtn_tick();
#endif
  boot_mark(8); leds_tick();
  boot_mark(9);

  while (USBSerial.available()) {
    char c = USBSerial.read();
    if (c == 'p') { if (pairing_active()) pairing_stop(); else pairing_start(); USBSerial.printf("pairing=%d\n", pairing_active()); }
    else if (c == 'f') { pairing_clear(); USBSerial.println("forgot all controllers"); }
    else if (c == 'v') { for (int i = 0; i < MAX_PADS; i++) gip_rumble(i, 70, 70, 0, 0); rumbleOff = millis() + 700; USBSerial.println("rumble on"); }
    else if (c == 'l') { static uint8_t m = 0; const uint8_t modes[] = {0, 1, 2, 3, 4, 8, 9, 1}; m = (m + 1) % sizeof(modes); for (int i = 0; i < MAX_PADS; i++) gip_led(i, modes[m], 20); USBSerial.printf("led mode %u\n", modes[m]); }
    else if (c == 's') status();
    else if (c == 'w') { int q = radio_tx_power() >= 78 ? 44 : radio_tx_power() + 17; radio_set_tx_power(q); USBSerial.printf("tx power set to %d (x0.25 dBm) = %.1f dBm\n", radio_tx_power(), radio_tx_power() / 4.0f); }
    else if (c == 'x') { static char cb[1800]; radio_census(0, cb, sizeof(cb)); USBSerial.printf("CENSUS (controller in slot 0)\n    %s", cb); }
    else if (c == 'k') { int f = radio_forced_channel(); f = f == 0 ? 1 : f == 1 ? 6 : f == 6 ? 11 : 0; radio_force_channel(f); USBSerial.printf("channel %s\n", f ? String(f).c_str() : "AUTO (scan)"); }
    else if (c == 'e') { radio_tweak(!radio_tweaked()); USBSerial.printf("radio tweaks %s\n", radio_tweaked() ? "ON" : "off"); }
    else if (c == 'z') { static char vb[3600]; radio_trace_vendor(vb, sizeof(vb)); USBSerial.printf("VENDOR FRAMES (type0/sub7), newest last\n%s", vb); }
#ifdef XINPUT_MODE
    else if (c == 'r') { static char rb[700]; xinput_dump(rb, sizeof(rb)); USBSerial.print(rb); }
#endif
    else if (c == 'm') { radio_set_filter(radio_filter() ? 0 : 1); USBSerial.printf("rx filter = %s\n", radio_filter() ? "management+data only" : "everything"); }
    else if (c == '1') { gip_test_ack(0, 0); USBSerial.println("test: rumble+ack"); }
    else if (c == '2') { gip_test_ack(0, 1); USBSerial.println("test: rumble(internal)+ack"); }
    else if (c == '3') { gip_test_ack(0, 2); USBSerial.println("test: led+ack"); }
    else if (c == 'd') { static bool d = false; d = !d; gip_set_debug(d); USBSerial.printf("gip debug %d\n", d); }
    else if (c == 'B') { USBSerial.println("rebooting into bootloader..."); USBSerial.flush(); delay(100); boot_to_download(); }
    else if (c == 'R') { USBSerial.println("restarting..."); USBSerial.flush(); delay(100); ESP.restart(); }
  }
  static uint32_t lastStat = 0;
  if (millis() - lastStat > 10000) { lastStat = millis(); status(); }
  delay(1);
}
