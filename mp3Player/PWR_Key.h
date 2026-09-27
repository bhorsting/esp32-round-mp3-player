#pragma once
#include <Arduino.h>
#include "Display_ST77916.h"

// Waveshare ESP32-S3-Touch-LCD-1.85 soft-latch (Key_BAT / BAT_Control)
#define PWR_KEY_Input_PIN   6
#define PWR_Control_PIN     7

// Long-press thresholds in PWR_Loop ticks (~100 ms each)
#define Device_Sleep_Time    10   // ~1.0 s (unused stub)
#define Device_Restart_Time  15   // ~1.5 s (unused stub)
#define Device_Shutdown_Time 20   // ~2.0 s → cut battery power

// LiPo soft cutoff (same floor as UI 0%). Debounced in PWR_Loop.
#define BAT_LOW_VOLTS        3.35f
#define BAT_LOW_COUNT_MAX    5    // consecutive low samples (~5 s at 1 Hz)

void Fall_Asleep(void);
void Restart(void);
void Shutdown(void);

void PWR_Init(void);
void PWR_Loop(void);

// Call ~once per second with the latest battery voltage.
void PWR_CheckBattery(float volts);
