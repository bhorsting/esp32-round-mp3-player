#pragma once

#include <Arduino.h>

// Hardware power savers that are safe for this player (audio + LVGL stay up).

// Force WiFi + Bluetooth controllers off and release their RAM.
// Call once early in setup(), after Serial is up.
void PowerSave_DisableRadios(void);

// Drop CPU from the default 240 MHz to 160 MHz — still plenty for MP3 + UI.
void PowerSave_SetCpuMhz(void);

// Idle screen power stages (backlight + panel DISPOFF).
enum ScreenPowerState : uint8_t {
  SCR_AWAKE = 0,
  SCR_DIMMED,
  SCR_ASLEEP,
};

void ScreenPower_Init(uint8_t brightness);
void ScreenPower_SetBrightness(uint8_t brightness);
void ScreenPower_SetState(ScreenPowerState next);
ScreenPowerState ScreenPower_GetState(void);
// Re-apply current state (e.g. after the user changes Brightness).
void ScreenPower_Apply(void);
