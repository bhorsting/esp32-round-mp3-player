#pragma once

#include <Arduino.h>
#include <lvgl.h>

// Lightweight fullscreen GIF overlay, played on track change.
//
// Split the same way CoverArt.cpp is: file/SD work happens off the LVGL
// task, LVGL object work only ever happens on the Driver_Loop task, and a
// small set of volatile flags hands state across that boundary - no mutex
// needed since each flag has exactly one writer and one reader, same
// trust model this codebase already relies on for CoverArt.

// Scans "/gif" on the SD card and builds the filename list in PSRAM.
// Call once from setup(), after SD_Init() - this is plain file I/O and
// must not touch LVGL.
void GifPlayer_ScanFiles();

// Creates the fullscreen overlay (on lv_layer_top(), above every screen)
// and wires up the touch-to-dismiss handler. Call once from Driver_Loop,
// after ui_init().
void GifPlayer_InitUI();

// Called from Play_Music_test() on the audio/main task whenever a new
// track actually starts (not on simple pause/resume). Picks the next GIF
// from the scanned list and requests it be shown - the actual file open
// and LVGL work happens later, in GifPlayer_Poll().
void GifPlayer_OnTrackChanged();

// Call every Driver_Loop tick. Opens a newly-requested GIF, advances the
// current animation by whatever frames are due, and loops playback
// until the overlay is dismissed (touch) or replaced (next track).
void GifPlayer_Poll();

// Play a specific GIF file at startup (before main UI). Loops indefinitely
// until clicked. Call this from Driver_Loop after GifPlayer_InitUI().
void GifPlayer_PlayStartup(const char *filename);
