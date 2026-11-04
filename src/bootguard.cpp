#include "bootguard.h"
#include "esp_system.h"
#include "esp_attr.h"
#include "soc/rtc_cntl_reg.h"

static RTC_NOINIT_ATTR uint32_t guardMagic;
static RTC_NOINIT_ATTR uint32_t crashCount;
static RTC_NOINIT_ATTR uint32_t lastMark;
static uint8_t prevMark = 0;
#define GUARD_MAGIC 0xB007CAFEu

void boot_mark(uint8_t step) { lastMark = (GUARD_MAGIC & 0xFFFFFF00u) | step; }
uint8_t boot_prev_mark() { return prevMark; }

void boot_to_download() {
  // Make the ROM boot straight into USB download mode on the next reset.
  REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
  esp_restart();
}

static const char *reasonName = "?";
const char *boot_reason() { return reasonName; }
uint32_t boot_crash_count() { return crashCount; }

void bootguard_begin() {
  pinMode(PIN_ENC_KEY, INPUT_PULLUP);
  esp_reset_reason_t r = esp_reset_reason();
  switch (r) {
    case ESP_RST_POWERON: reasonName = "poweron"; break;
    case ESP_RST_SW: reasonName = "software"; break;
    case ESP_RST_PANIC: reasonName = "PANIC(crash)"; break;
    case ESP_RST_INT_WDT: reasonName = "int-watchdog"; break;
    case ESP_RST_TASK_WDT: reasonName = "task-watchdog"; break;
    case ESP_RST_WDT: reasonName = "watchdog"; break;
    case ESP_RST_BROWNOUT: reasonName = "BROWNOUT(power dip)"; break;
    case ESP_RST_EXT: reasonName = "reset-button"; break;
    case ESP_RST_DEEPSLEEP: reasonName = "deepsleep"; break;
    default: reasonName = "other"; break;
  }
  if (guardMagic != GUARD_MAGIC) { guardMagic = GUARD_MAGIC; crashCount = 0; lastMark = 0; }
  prevMark = ((lastMark & 0xFFFFFF00u) == (GUARD_MAGIC & 0xFFFFFF00u)) ? (lastMark & 0xFF) : 0;  // RTC memory is garbage after power-on
  bool bad = r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT || r == ESP_RST_BROWNOUT;
  if (r == ESP_RST_POWERON) crashCount = 0;
  else if (bad) crashCount++;
  if (crashCount >= 3) { crashCount = 0; boot_to_download(); }
}

static uint32_t downSince = 0;
float boot_hold_progress() { return downSince ? (float)(millis() - downSince) / 10000.0f : 0.0f; }

void bootguard_tick() {
  if (crashCount && millis() > 10000) crashCount = 0;   // survived 10 s: healthy boot
  if (digitalRead(PIN_ENC_KEY) == LOW) {
    if (!downSince) downSince = millis();
    else if (millis() - downSince > 10000) boot_to_download();
  } else {
    downSince = 0;
  }
}
