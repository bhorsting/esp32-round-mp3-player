#include "CoverArt.h"

#include <JPEGDEC.h>
#include <esp_heap_caps.h>
#include <string.h>

// Studio sets width 175 and height CONTENT — use width as the viewport
// (square) unless a fixed height was exported.
static const lv_coord_t COVER_FALLBACK = 175;

static lv_obj_t *s_viewport = nullptr;
static lv_obj_t *s_img = nullptr;
static lv_img_dsc_t s_dsc;
static uint16_t *s_pixels = nullptr;  // full cover-scaled RGB565 (may exceed viewport)
static uint16_t *s_decode = nullptr;
static int s_decodeW = 0;
static int s_decodeH = 0;
static int s_outW = 0;
static int s_outH = 0;
static int s_viewW = COVER_FALLBACK;
static int s_viewH = COVER_FALLBACK;
static volatile bool s_pending = false;
static volatile bool s_hasArt = false;
static JPEGDEC s_jpeg;

static void *coverAlloc(size_t n) {
  void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = malloc(n);
  return p;
}

static void coverFree(void *p) {
  if (p) free(p);
}

static int jpegDraw(JPEGDRAW *p) {
  if (!s_decode || !p || !p->pPixels) return 0;
  for (int y = 0; y < p->iHeight; y++) {
    int dy = p->y + y;
    if (dy < 0 || dy >= s_decodeH) continue;
    int copyW = p->iWidth;
    if (p->x + copyW > s_decodeW) copyW = s_decodeW - p->x;
    if (copyW <= 0 || p->x < 0) continue;
    memcpy(&s_decode[dy * s_decodeW + p->x],
           &((uint16_t *)p->pPixels)[y * p->iWidth],
           (size_t)copyW * 2);
  }
  return 1;
}

// MIME is always latin1 $00; description follows text encoding.
static size_t skipApicHeader(const uint8_t *buf, size_t size) {
  if (size < 4) return 0;
  size_t i = 0;
  uint8_t enc = buf[i++];

  while (i < size && buf[i]) i++;
  if (i < size) i++;
  if (i >= size) return 0;

  i++; // picture type
  if (i >= size) return 0;

  if (enc == 1 || enc == 2) {
    while (i + 1 < size && (buf[i] || buf[i + 1])) i += 2;
    if (i + 1 < size) i += 2;
  } else {
    while (i < size && buf[i]) i++;
    if (i < size) i++;
  }

  for (size_t j = i; j + 1 < size; j++) {
    if (buf[j] == 0xFF && buf[j + 1] == 0xD8) return j;
  }
  for (size_t j = 0; j + 1 < size; j++) {
    if (buf[j] == 0xFF && buf[j + 1] == 0xD8) return j;
  }
  return i;
}

// Scale decoded pixels with object-fit:cover into at least viewW x viewH
// (result may be larger on one axis so the viewport can scroll).
static bool scaleCoverFit(const uint16_t *src, int sw, int sh, int viewW, int viewH) {
  if (!src || sw <= 0 || sh <= 0 || viewW <= 0 || viewH <= 0) return false;

  float scale = (float)viewW / (float)sw;
  float scaleY = (float)viewH / (float)sh;
  if (scaleY > scale) scale = scaleY;

  int outW = (int)((float)sw * scale + 0.5f);
  int outH = (int)((float)sh * scale + 0.5f);
  if (outW < viewW) outW = viewW;
  if (outH < viewH) outH = viewH;

  coverFree(s_pixels);
  s_pixels = (uint16_t *)coverAlloc((size_t)outW * (size_t)outH * 2);
  if (!s_pixels) return false;

  for (int y = 0; y < outH; y++) {
    int sy = (int)((y + 0.5f) * (float)sh / (float)outH);
    if (sy < 0) sy = 0;
    if (sy >= sh) sy = sh - 1;
    for (int x = 0; x < outW; x++) {
      int sx = (int)((x + 0.5f) * (float)sw / (float)outW);
      if (sx < 0) sx = 0;
      if (sx >= sw) sx = sw - 1;
      s_pixels[y * outW + x] = src[sy * sw + sx];
    }
  }

  s_outW = outW;
  s_outH = outH;
  memset(&s_dsc, 0, sizeof(s_dsc));
  s_dsc.header.always_zero = 0;
  s_dsc.header.w = outW;
  s_dsc.header.h = outH;
  s_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
  s_dsc.data_size = (uint32_t)outW * outH * 2;
  s_dsc.data = (const uint8_t *)s_pixels;
  return true;
}

static bool decodeJpegToCover(const uint8_t *jpg, size_t jpgLen, int viewW, int viewH) {
  if (!jpg || jpgLen < 4) return false;
  if (jpg[0] != 0xFF || jpg[1] != 0xD8) {
    printf("CoverArt: not JPEG (skip PNG/other)\r\n");
    return false;
  }

  if (s_jpeg.openRAM((uint8_t *)jpg, (int)jpgLen, jpegDraw) != 1) {
    printf("CoverArt: JPEG open failed\r\n");
    return false;
  }

  int iw = s_jpeg.getWidth();
  int ih = s_jpeg.getHeight();
  if (iw <= 0 || ih <= 0) {
    s_jpeg.close();
    return false;
  }

  int scaleFlag = 0;
  int dw = iw, dh = ih;
  const int maxSide = 320;
  if (iw > maxSide * 8 || ih > maxSide * 8) {
    scaleFlag = JPEG_SCALE_EIGHTH;
    dw = iw / 8;
    dh = ih / 8;
  } else if (iw > maxSide * 4 || ih > maxSide * 4) {
    scaleFlag = JPEG_SCALE_QUARTER;
    dw = iw / 4;
    dh = ih / 4;
  } else if (iw > maxSide * 2 || ih > maxSide * 2) {
    scaleFlag = JPEG_SCALE_HALF;
    dw = iw / 2;
    dh = ih / 2;
  }
  if (dw < 1) dw = 1;
  if (dh < 1) dh = 1;

  coverFree(s_decode);
  s_decode = (uint16_t *)coverAlloc((size_t)dw * (size_t)dh * 2);
  if (!s_decode) {
    s_jpeg.close();
    printf("CoverArt: decode alloc failed (%dx%d)\r\n", dw, dh);
    return false;
  }
  memset(s_decode, 0, (size_t)dw * (size_t)dh * 2);
  s_decodeW = dw;
  s_decodeH = dh;

  s_jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
  if (s_jpeg.decode(0, 0, scaleFlag) != 1) {
    s_jpeg.close();
    coverFree(s_decode);
    s_decode = nullptr;
    printf("CoverArt: JPEG decode failed\r\n");
    return false;
  }
  s_jpeg.close();

  bool ok = scaleCoverFit(s_decode, s_decodeW, s_decodeH, viewW, viewH);
  coverFree(s_decode);
  s_decode = nullptr;
  s_decodeW = s_decodeH = 0;
  return ok;
}

static void ensureChildImg() {
  if (!s_viewport || s_img) return;
  s_img = lv_img_create(s_viewport);
  lv_obj_set_pos(s_img, 0, 0);
  lv_obj_clear_flag(s_img, LV_OBJ_FLAG_CLICKABLE);
}

void CoverArt_begin(lv_obj_t *viewport) {
  s_viewport = viewport;
  if (!s_viewport) return;

  lv_coord_t w = lv_obj_get_style_width(s_viewport, LV_PART_MAIN);
  lv_coord_t h = lv_obj_get_style_height(s_viewport, LV_PART_MAIN);
  if (w <= 1) w = COVER_FALLBACK;
  // Only fall back to square when Studio used CONTENT height (~1).
  if (h <= 1) h = w;
  s_viewW = (int)w;
  s_viewH = (int)h;
  printf("CoverArt: viewport %dx%d\r\n", s_viewW, s_viewH);

  // Use the Studio widget as a clipped, scrollable viewport — not as the image itself
  // (lv_img_set_src would expand CONTENT height past the frame).
  lv_img_set_src(s_viewport, NULL);
  lv_obj_set_size(s_viewport, s_viewW, s_viewH);
  lv_obj_set_style_pad_all(s_viewport, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(s_viewport, 8, LV_PART_MAIN);
  lv_obj_set_style_clip_corner(s_viewport, true, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_viewport, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_viewport, 0, LV_PART_MAIN);

  lv_obj_clear_flag(s_viewport, LV_OBJ_FLAG_ADV_HITTEST);
  lv_obj_add_flag(s_viewport, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(s_viewport, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(s_viewport, LV_DIR_ALL);
  lv_obj_set_scrollbar_mode(s_viewport, LV_SCROLLBAR_MODE_OFF);

  ensureChildImg();

  if (!s_hasArt) lv_obj_add_flag(s_viewport, LV_OBJ_FLAG_HIDDEN);
  else s_pending = true;
}

void CoverArt_clear() {
  s_pending = false;
  s_hasArt = false;
  if (s_img) lv_img_set_src(s_img, NULL);
  if (s_viewport) lv_obj_add_flag(s_viewport, LV_OBJ_FLAG_HIDDEN);
}

void CoverArt_onId3Image(File &file, size_t pos, size_t size) {
  if (size == 0 || size > 512 * 1024) {
    printf("CoverArt: skip size %u\r\n", (unsigned)size);
    return;
  }

  int viewW = s_viewW > 0 ? s_viewW : COVER_FALLBACK;
  int viewH = s_viewH > 0 ? s_viewH : COVER_FALLBACK;

  uint8_t *raw = (uint8_t *)coverAlloc(size);
  if (!raw) {
    printf("CoverArt: APIC alloc failed\r\n");
    return;
  }

  size_t cur = file.position();
  if (!file.seek(pos)) {
    coverFree(raw);
    return;
  }
  size_t got = file.read(raw, size);
  file.seek(cur);
  if (got < size) size = got;

  size_t off = skipApicHeader(raw, size);
  if (off >= size) {
    coverFree(raw);
    return;
  }

  bool ok = decodeJpegToCover(raw + off, size - off, viewW, viewH);
  coverFree(raw);
  if (ok) {
    s_hasArt = true;
    s_pending = true;
    printf("CoverArt: ready %dx%d in %dx%d viewport\r\n", s_outW, s_outH, viewW, viewH);
  }
}

void CoverArt_poll() {
  if (!s_pending || !s_viewport) return;
  s_pending = false;
  if (!s_hasArt || !s_pixels) {
    CoverArt_clear();
    return;
  }

  ensureChildImg();
  // Re-assert viewport size — image src must not grow the frame.
  lv_obj_set_size(s_viewport, s_viewW, s_viewH);
  lv_img_set_src(s_img, &s_dsc);
  lv_obj_set_size(s_img, s_outW, s_outH);
  lv_img_set_zoom(s_img, 256);
  lv_obj_clear_flag(s_viewport, LV_OBJ_FLAG_HIDDEN);

  // Start centered so the crop matches object-fit:cover; user can pan.
  lv_coord_t sx = 0;
  lv_coord_t sy = 0;
  if (s_outW > s_viewW) sx = (s_outW - s_viewW) / 2;
  if (s_outH > s_viewH) sy = (s_outH - s_viewH) / 2;
  lv_obj_scroll_to(s_viewport, sx, sy, LV_ANIM_OFF);
}
