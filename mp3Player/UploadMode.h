#pragma once

#include <Arduino.h>
#include <lvgl.h>

// Fullscreen "USB Upload" overlay. Entered when the web file manager
// connects, exited on disconnect. File/audio work happens on the audio
// task; LVGL object work only on the Driver_Loop task (same flag pattern
// as GifPlayer / CoverArt).

void UploadMode_InitUI();

// Call from USB/audio task (under audio_mutex). Stops GIF request and
// marks upload mode active; LVGL shows the overlay on the next Poll.
void UploadMode_Enter();

// Call from USB/audio task. Clears active flag and requests a library
// rescan + UI refresh + playback restart (handled by the main loop /
// Driver_Loop via the helpers below).
void UploadMode_Exit();

bool UploadMode_IsActive();

// True once after Exit until the audio task finishes rescanning the SD
// library. Cleared by UploadMode_ClearRescanRequest().
bool UploadMode_NeedsRescan();
void UploadMode_ClearRescanRequest();

// True once after Exit until Driver_Loop refreshes the roller / play icon.
bool UploadMode_NeedsUiRefresh();
void UploadMode_ClearUiRefresh();

// True once after Exit until the audio task starts playback again.
bool UploadMode_NeedsRestart();
void UploadMode_ClearRestart();

// Call every Driver_Loop tick: show/hide overlay to match active state.
void UploadMode_Poll();
