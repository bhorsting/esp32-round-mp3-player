#include "UploadMode.h"

static const int SCREEN_W = 360;
static const int SCREEN_H = 360;

static lv_obj_t *s_overlay = nullptr;
static lv_obj_t *s_icon = nullptr;
static lv_obj_t *s_label = nullptr;

static volatile bool s_active = false;
static volatile bool s_wantVisible = false;
static volatile bool s_needsRescan = false;
static volatile bool s_needsUiRefresh = false;
static volatile bool s_needsRestart = false;
static bool s_visible = false;

void UploadMode_InitUI() {
  s_overlay = lv_obj_create(lv_layer_top());
  lv_obj_set_size(s_overlay, SCREEN_W, SCREEN_H);
  lv_obj_set_pos(s_overlay, 0, 0);
  lv_obj_set_style_pad_all(s_overlay, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_overlay, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(s_overlay, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_overlay, lv_color_hex(0x0a0a0a), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
  // Block touches from reaching player controls underneath.
  lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);

  s_icon = lv_label_create(s_overlay);
  // DOWNLOAD = arrow into tray — files landing on the device.
  // Symbols live in the Montserrat glyph set (unscii has ASCII only).
  lv_label_set_text(s_icon, LV_SYMBOL_DOWNLOAD);
  lv_obj_set_style_text_color(s_icon, lv_color_hex(0xffffff), LV_PART_MAIN);
  lv_obj_set_style_text_font(s_icon, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_align(s_icon, LV_ALIGN_CENTER, 0, -28);

  s_label = lv_label_create(s_overlay);
  lv_label_set_text(s_label, "USB Upload");
  lv_obj_set_style_text_color(s_label, lv_color_hex(0xcccccc), LV_PART_MAIN);
  lv_obj_set_style_text_font(s_label, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_align(s_label, LV_ALIGN_CENTER, 0, 8);

  lv_obj_t *hint = lv_label_create(s_overlay);
  lv_label_set_text(hint, "Connected");
  lv_obj_set_style_text_color(hint, lv_color_hex(0x666666), LV_PART_MAIN);
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, LV_PART_MAIN);
  lv_obj_align(hint, LV_ALIGN_CENTER, 0, 32);
}

void UploadMode_Enter() {
  if (s_active) return;
  s_active = true;
  s_wantVisible = true;
  s_needsRescan = false;
  s_needsUiRefresh = false;
  s_needsRestart = false;
}

void UploadMode_Exit() {
  if (!s_active) return;
  s_active = false;
  s_wantVisible = false;
  s_needsRescan = true;
  s_needsUiRefresh = true;
  s_needsRestart = true;
}

bool UploadMode_IsActive() { return s_active; }

bool UploadMode_NeedsRescan() { return s_needsRescan; }
void UploadMode_ClearRescanRequest() { s_needsRescan = false; }

bool UploadMode_NeedsUiRefresh() { return s_needsUiRefresh; }
void UploadMode_ClearUiRefresh() { s_needsUiRefresh = false; }

bool UploadMode_NeedsRestart() { return s_needsRestart; }
void UploadMode_ClearRestart() { s_needsRestart = false; }

void UploadMode_Poll() {
  if (!s_overlay) return;

  if (s_wantVisible && !s_visible) {
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_overlay);
    s_visible = true;
  } else if (!s_wantVisible && s_visible) {
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    s_visible = false;
  }
}
