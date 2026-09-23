#pragma once

#include <Arduino.h>
#include <FS.h>
#include <lvgl.h>

void CoverArt_begin(lv_obj_t *viewport);
void CoverArt_clear();
// Called from audio_id3image on the audio task — must not touch LVGL.
void CoverArt_onId3Image(File &file, size_t pos, size_t size);
// Called from the LVGL task — applies a pending decode to the viewport.
void CoverArt_poll();
