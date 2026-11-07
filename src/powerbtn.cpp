#include "config.h"
#if HAS_PMIC
#include "powerbtn.h"
#include "power.h"
#include "leds.h"
#include "ui.h"
#include "hid.h"
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"

#define PWRKEY_PIN 6
#define PWRKEY_HOLD_MS 5000

static uint32_t downAt = 0;
static bool lastDown = false;

void powerbtn_begin() {
  // After deep sleep the pins we parked are still held: release them.
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis((gpio_num_t)PIN_PWR_EN);
  gpio_hold_dis((gpio_num_t)PIN_LCD_BL);
  rtc_gpio_deinit((gpio_num_t)PWRKEY_PIN);   // hand the pad back to the normal GPIO matrix after a wake
  pinMode(PWRKEY_PIN, INPUT_PULLUP);
}

float power_hold_progress() { return downAt ? (float)(millis() - downAt) / PWRKEY_HOLD_MS : 0.0f; }

static void powerOff() {
  USBSerial.println("deep sleep (press the top key to wake)");
  USBSerial.flush();
  leds_off();
  ui_off();
  esp_wifi_stop();
  while (digitalRead(PWRKEY_PIN) == LOW) delay(10);   // wait for release, or the wake-on-low would fire at once
  delay(150);

  // Backlight off and held off. The peripheral power rail (PWR_EN) stays ON and held: the top key's pull-up and
  // the wake path must keep working, and cutting that rail is what could leave the board unable to wake.
  pinMode(PIN_LCD_BL, OUTPUT); digitalWrite(PIN_LCD_BL, LOW);
  pinMode(PIN_PWR_EN, OUTPUT); digitalWrite(PIN_PWR_EN, HIGH);
  gpio_hold_en((gpio_num_t)PIN_LCD_BL);
  gpio_hold_en((gpio_num_t)PIN_PWR_EN);
  gpio_deep_sleep_hold_en();
  // Wake on the top key (GPIO6, low). Set the RTC pad up fully, with its own pull-up, before arming the wake source.
  rtc_gpio_init((gpio_num_t)PWRKEY_PIN);
  rtc_gpio_set_direction((gpio_num_t)PWRKEY_PIN, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pulldown_dis((gpio_num_t)PWRKEY_PIN);
  rtc_gpio_pullup_en((gpio_num_t)PWRKEY_PIN);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PWRKEY_PIN, 0);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);   // the wake pad lives in this domain
  esp_deep_sleep_start();
}

static volatile bool backFlag = false;
bool powerbtn_take_back() { bool b = backFlag; backFlag = false; return b; }
void powerbtn_sleep_now() { powerOff(); }

void powerbtn_tick() {
  bool down = digitalRead(PWRKEY_PIN) == LOW;
  uint32_t now = millis();
  if (down != lastDown) {
    static uint32_t edge = 0;
    if (now - edge > 30) {
      edge = now; lastDown = down;
      if (!down && downAt && now - downAt < 900) backFlag = true;   // short press = back
      downAt = down ? now : 0;
    }
  }
  if (downAt && now - downAt >= PWRKEY_HOLD_MS) powerOff();
}
#endif
