#include "GifPlayer.h"

#include <AnimatedGIF.h>
#include <esp_heap_caps.h>
#include "FS.h"
#include "SD_MMC.h"

// Root folder scanned once at boot - not recursive, matches "root GIF
// folder" as specified.
static const char *GIF_DIR = "/GIF";
static const int MAX_GIFS = 200;
static const int GIF_NAME_LEN = 64;

static const int SCREEN_W = 360;
static const int SCREEN_H = 360;

// Filename list lives in one contiguous PSRAM block, indexed like a 2D
// array (mirrors SD_Card.cpp's Folder_retrieval() char[][N] pattern, but
// PSRAM-backed instead of a fixed global in internal RAM).
static char *s_gifNames = nullptr;
static int s_gifCount = 0;
static int s_lastGifPlayed = -1;  // avoid repeating the same GIF twice in a row

static inline char *gifName(int i) { return s_gifNames + (size_t)i * GIF_NAME_LEN; }

static void *gifAlloc(size_t n) {
  void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = malloc(n);
  return p;
}

static bool isGifName(const char *name) {
  size_t len = strlen(name);
  if (len < 4) return false;
  const char *ext = name + len - 4;
  return (ext[0] == '.' &&
          (ext[1] == 'g' || ext[1] == 'G') &&
          (ext[2] == 'i' || ext[2] == 'I') &&
          (ext[3] == 'f' || ext[3] == 'F'));
}

void GifPlayer_ScanFiles() {
  s_gifNames = (char *)gifAlloc((size_t)MAX_GIFS * GIF_NAME_LEN);
  if (!s_gifNames) {
    printf("GifPlayer: PSRAM alloc for filename list failed\r\n");
    return;
  }

  File dir = SD_MMC.open(GIF_DIR);
  if (!dir || !dir.isDirectory()) {
    printf("GifPlayer: %s not found, no GIFs available\r\n", GIF_DIR);
    return;
  }

  File file = dir.openNextFile();
  while (file && s_gifCount < MAX_GIFS) {
    if (!file.isDirectory()) {
      const char *name = file.name();
      // SD_MMC File::name() on a directory-opened File returns the bare
      // filename (no path prefix) - store it bare and join with GIF_DIR
      // again when opening, same convention as the rest of this codebase.
      const char *base = name;
      const char *slash = strrchr(name, '/');
      if (slash) base = slash + 1;

      if (isGifName(base)) {
        strncpy(gifName(s_gifCount), base, GIF_NAME_LEN - 1);
        gifName(s_gifCount)[GIF_NAME_LEN - 1] = '\0';
        s_gifCount++;
      }
    }
    file = dir.openNextFile();
  }
  dir.close();

  printf("GifPlayer: found %d GIF(s) in %s\r\n", s_gifCount, GIF_DIR);
}

// ---- LVGL overlay -----------------------------------------------------

static lv_obj_t *s_overlay = nullptr;   // fullscreen clickable container
static lv_obj_t *s_canvas = nullptr;    // pixel surface
static uint16_t *s_canvasBuf = nullptr; // PSRAM RGB565 framebuffer, SCREEN_W x SCREEN_H

static AnimatedGIF s_gif;
static File s_gifFile;
static bool s_gifOpen = false;
static bool s_gifActive = false;   // currently decoding/showing frames
static int s_gifOffX = 0, s_gifOffY = 0;
static unsigned long s_nextFrameDueMs = 0;

static volatile bool s_startRequested = false;  // set on audio task, cleared on LVGL task

// ---- AnimatedGIF file I/O callbacks (wrap Arduino File/SD_MMC) --------

static void *GIFOpenFile(const char *fname, int32_t *pFileSize) {
  s_gifFile = SD_MMC.open(fname);
  if (!s_gifFile) return nullptr;
  *pFileSize = (int32_t)s_gifFile.size();
  return &s_gifFile;
}

static void GIFCloseFile(void *pHandle) {
  File *f = (File *)pHandle;
  if (f) f->close();
}

// The library tracks its own read cursor in pFile->iPos rather than
// asking the File object for it - GIFParseInfo/the LZW decoder use that
// field directly to decide how much data remains and where to seek for
// the next frame. It is the callback's job to keep it in sync after every
// read/seek; leaving it frozen (as an earlier version of this file did)
// desyncs the decoder's bookkeeping from the real file position after
// the very first frame, so every later playFrame() call silently fails
// (returns -1) while the canvas just keeps showing frame 1 forever, with
// no crash or hang to make the cause obvious. Mirrors the library's own
// bundled GIFPlayer.h reference callbacks exactly, including its
// documented seek-at-EOF workaround.
static int32_t GIFReadFile(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen) {
  File *f = (File *)pFile->fHandle;
  if (!f) return 0;
  if ((pFile->iSize - pFile->iPos) < iLen) {
    iLen = pFile->iSize - pFile->iPos - 1;
  }
  if (iLen <= 0) return 0;
  iLen = (int32_t)f->read(pBuf, iLen);
  pFile->iPos = (int32_t)f->position();
  return iLen;
}

static int32_t GIFSeekFile(GIFFILE *pFile, int32_t iPosition) {
  File *f = (File *)pFile->fHandle;
  if (!f) return 0;
  f->seek(iPosition);
  pFile->iPos = (int32_t)f->position();
  return pFile->iPos;
}

// Draw one decoded scanline into our fullscreen PSRAM framebuffer,
// centering the GIF's own logical canvas and clipping anything that
// falls outside the screen - no scaling, kept deliberately simple
// ("lightweight"). Transparent pixels leave whatever's already in the
// buffer (the black background, or a prior frame's pixel for partial
// updates) rather than being overwritten.
static void GIFDraw(GIFDRAW *pDraw) {
  if (!s_canvasBuf) return;

  int destY = s_gifOffY + pDraw->iY + pDraw->y;
  if (destY < 0 || destY >= SCREEN_H) return;

  uint16_t *palette = pDraw->pPalette;
  uint16_t *destRow = s_canvasBuf + (size_t)destY * SCREEN_W;

  for (int x = 0; x < pDraw->iWidth; x++) {
    int destX = s_gifOffX + pDraw->iX + x;
    if (destX < 0 || destX >= SCREEN_W) continue;

    uint8_t idx = pDraw->pPixels[x];
    if (pDraw->ucHasTransparency && idx == pDraw->ucTransparent) continue;

    destRow[destX] = palette[idx];
  }
}

void GifPlayer_InitUI() {
  s_canvasBuf = (uint16_t *)gifAlloc((size_t)SCREEN_W * SCREEN_H * 2);
  if (!s_canvasBuf) {
    printf("GifPlayer: framebuffer alloc failed, GIF overlay disabled\r\n");
    return;
  }
  memset(s_canvasBuf, 0, (size_t)SCREEN_W * SCREEN_H * 2);

  s_overlay = lv_obj_create(lv_layer_top());
  lv_obj_set_size(s_overlay, SCREEN_W, SCREEN_H);
  lv_obj_set_pos(s_overlay, 0, 0);
  lv_obj_set_style_pad_all(s_overlay, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_overlay, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(s_overlay, 0, LV_PART_MAIN);
  lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);

  s_canvas = lv_canvas_create(s_overlay);
  lv_canvas_set_buffer(s_canvas, s_canvasBuf, SCREEN_W, SCREEN_H, LV_IMG_CF_TRUE_COLOR);
  lv_obj_set_pos(s_canvas, 0, 0);
  lv_obj_clear_flag(s_canvas, LV_OBJ_FLAG_CLICKABLE);

  // Any tap anywhere on the overlay dismisses it immediately - audio just
  // keeps playing throughout, this is purely a visual layer.
  lv_obj_add_event_cb(s_overlay, [](lv_event_t *e) {
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    s_gifActive = false;
    if (s_gifOpen) {
      s_gif.close();
      s_gifOpen = false;
    }
  }, LV_EVENT_PRESSED, NULL);

  s_gif.begin(LITTLE_ENDIAN_PIXELS);
}

void GifPlayer_OnTrackChanged() {
  if (s_gifCount <= 0) return;
  s_startRequested = true;
}

static void startGifByPath(const char *path) {
  if (s_gifOpen) {
    s_gif.close();
    s_gifOpen = false;
  }

  if (!s_gif.open(path, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw)) {
    printf("GifPlayer: failed to open %s\r\n", path);
    s_gifActive = false;
    return;
  }
  s_gifOpen = true;

  int gw = s_gif.getCanvasWidth();
  int gh = s_gif.getCanvasHeight();
  s_gifOffX = (SCREEN_W - gw) / 2;
  s_gifOffY = (SCREEN_H - gh) / 2;

  memset(s_canvasBuf, 0, (size_t)SCREEN_W * SCREEN_H * 2);
  lv_obj_invalidate(s_canvas);

  lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s_overlay);
  s_gifActive = true;
  s_nextFrameDueMs = millis();

  printf("GifPlayer: playing %s (%dx%d)\r\n", path, gw, gh);
}

static void startNextGif() {
  int index = random(s_gifCount);
  // Resample once if we happen to land on the one that's already playing -
  // with a small list, a same-again pick is common enough to feel broken
  // rather than "random," so it's worth one extra roll to avoid it.
  if (s_gifCount > 1 && index == s_lastGifPlayed) {
    index = random(s_gifCount);
  }
  s_lastGifPlayed = index;

  char path[16 + GIF_NAME_LEN];
  snprintf(path, sizeof(path), "%s/%s", GIF_DIR, gifName(index));
  startGifByPath(path);
}

void GifPlayer_Poll() {
  if (!s_canvasBuf || !s_overlay) return;

  // Only process track-change GIF requests if we're not already playing a GIF
  // (allows startup GIF to play uninterrupted)
  if (s_startRequested && !s_gifActive) {
    s_startRequested = false;
    startNextGif();
  }

  if (!s_gifActive || !s_gifOpen) return;
  if ((long)(millis() - s_nextFrameDueMs) < 0) return;

  int delayMs = 0;
  int result = s_gif.playFrame(false, &delayMs, NULL);
  lv_obj_invalidate(s_canvas);

  if (result == 0) {
    // End of animation - loop forever (without closing/reopening the SD
    // file) until dismissed by touch or replaced by the next track.
    s_gif.reset();
    s_nextFrameDueMs = millis();
    return;
  }

  s_nextFrameDueMs = millis() + (delayMs > 0 ? delayMs : 10);
}

void GifPlayer_PlayStartup(const char *filename) {
  if (!s_canvasBuf || !s_overlay) return;

  char path[16 + GIF_NAME_LEN];
  snprintf(path, sizeof(path), "%s/%s", GIF_DIR, filename);
  startGifByPath(path);
}
