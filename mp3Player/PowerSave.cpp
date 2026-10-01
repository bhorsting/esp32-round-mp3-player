#include "PowerSave.h"

#include <Arduino.h>
#include <WiFi.h>
#include "esp_bt.h"
#include "esp_wifi.h"
#include "Display_ST77916.h"
#include "GifPlayer.h"

void PowerSave_DisableRadios(void) {
  // WiFi — force the radio off. stop/deinit are no-ops / error if never
  // started; ignore return codes so a cold boot stays quiet.
  WiFi.mode(WIFI_OFF);
  WiFi.disconnect(true);
  esp_wifi_stop();
  esp_wifi_deinit();

  // Bluetooth — Arduino helper first, then release controller + host RAM
  // (~70 KB+) so it cannot be woken for the rest of this boot.
  btStop();
  esp_bt_controller_disable();
  esp_bt_controller_deinit();
  esp_bt_mem_release(ESP_BT_MODE_BTDM);
}

void PowerSave_SetCpuMhz(void) {
  // 160 MHz is enough for ESP32-audioI2S MP3 decode + LVGL on this board
  // and draws less than the default 240 MHz.
  setCpuFrequencyMhz(160);
}

static ScreenPowerState s_screenPower = SCR_AWAKE;
static uint8_t s_brightness = 30;

static uint8_t dimmedBrightness(uint8_t full) {
  uint8_t half = full / 2;
  return half < 1 ? 1 : half;
}

void ScreenPower_Apply(void) {
  switch (s_screenPower) {
    case SCR_AWAKE:
      LCD_Wake();
      Set_Backlight(s_brightness);
      break;
    case SCR_DIMMED:
      LCD_Wake();
      Set_Backlight(dimmedBrightness(s_brightness));
      break;
    case SCR_ASLEEP:
      Set_Backlight(0);
      LCD_Sleep();
      // Stop GIF decode/SD while the panel is dark.
      GifPlayer_Dismiss();
      break;
  }
}

void ScreenPower_Init(uint8_t brightness) {
  s_brightness = brightness;
  s_screenPower = SCR_AWAKE;
  ScreenPower_Apply();
}

void ScreenPower_SetBrightness(uint8_t brightness) {
  s_brightness = brightness;
}

void ScreenPower_SetState(ScreenPowerState next) {
  if (s_screenPower == next) return;
  s_screenPower = next;
  ScreenPower_Apply();
}

ScreenPowerState ScreenPower_GetState(void) {
  return s_screenPower;
}
