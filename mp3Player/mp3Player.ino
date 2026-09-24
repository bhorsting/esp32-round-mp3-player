#include "SD_Card.h"
#include "Audio.h"
#include "ui.h"
#include "LVGL_Driver.h"
#include "BAT_Driver.h"
#include "CoverArt.h"
#include "GifPlayer.h"
#include <ESP32Time.h>
#include "USBProtocol.h"
#include "USBCommandDispatcher.h"
#include "PreferencesManager.h"
#include "Display_ST77916.h"

ESP32Time rtc(0);

SemaphoreHandle_t audio_mutex;

#define I2S_DOUT      47
#define I2S_BCLK      48
#define I2S_LRC       38

#define MAX_FILES 150
String audioFiles[MAX_FILES];
String audioFilesShort[MAX_FILES];

int fileCount = 0;
bool changeIsMade = 0;
bool playPressed = 0;
bool stopPressed = 0;
bool nextPressed = 0;
bool prevPressed = 0;
bool volumePressed = 0;
bool isPlaying = 1;

int rolerIndex = 0;
int chosenFile = 0;
int playingIndex = -1;
String playingSong = "";
Audio audio;
uint8_t Volume = 10;
uint8_t Brightness = 50;
bool brightnessControlMode = false;

unsigned long batTime = 0;
int deb = 0;

// Double-tap detection for brightness mode toggle (on track area)
unsigned long lastTrackPress = 0;
unsigned long brightnessActivatedTime = 0;
#define DOUBLE_TAP_THRESHOLD 500
#define BRIGHTNESS_MODE_TIMEOUT 5000

// USB file manager globals
#define USB_CMD_BUFFER_SIZE 512
uint8_t usb_cmd_buffer[USB_CMD_BUFFER_SIZE];
uint16_t usb_cmd_buffer_pos = 0;
bool usb_cmd_complete = false;
unsigned long usb_last_byte_time = 0;
#define USB_CMD_TIMEOUT_MS 1000

void resetClock()
{
  rtc.setTime(0, 0, 0, 17, 1, 2021);
}

void Play_Music_test() {
  if (fileCount <= 0) {
    printf("Music Read Failed: no mp3 files found on SD\r\n");
    return;
  }
  if (chosenFile < 0 || chosenFile >= fileCount) chosenFile = 0;

  CoverArt_clear();
  bool ret = audio.connecttoFS(SD_MMC, audioFiles[chosenFile].c_str());
  playingIndex = chosenFile;
  playingSong = audioFiles[chosenFile];
  int slash = playingSong.lastIndexOf('/');
  if (slash >= 0) playingSong = playingSong.substring(slash + 1);
  int dot = playingSong.lastIndexOf('.');
  if (dot > 0) playingSong = playingSong.substring(0, dot);

  if (ret) {
    printf("Music Read OK: %s\r\n", audioFiles[chosenFile].c_str());
    preferencesManager.setLastTrack(audioFiles[chosenFile]);
    GifPlayer_OnTrackChanged();
  } else {
    printf("Music Read Failed: %s\r\n", audioFiles[chosenFile].c_str());
  }
}

void audio_id3image(File &file, const size_t pos, const size_t size) {
  CoverArt_onId3Image(file, pos, size);
}

void Play_Music_Next() {
  if (fileCount <= 0) {
    printf("Music Read Failed: no mp3 files found on SD\r\n");
    return;
  }
  chosenFile++;
  if (chosenFile >= fileCount) chosenFile = 0;
  Play_Music_test();
}

void Play_Music_Prev() {
  if (fileCount <= 0) {
    printf("Music Read Failed: no mp3 files found on SD\r\n");
    return;
  }
  chosenFile--;
  if (chosenFile < 0) chosenFile = fileCount - 1;
  Play_Music_test();
}

void Audio_Init() {
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.setVolume(Volume); // 0...21
  audio.setBufsize(64 * 1024, 32 * 1024);
}

static String joinPath(const String &dir, const String &name) {
  if (name.startsWith("/")) return name;
  if (dir == "/") return "/" + name;
  if (dir.endsWith("/")) return dir + name;
  return dir + "/" + name;
}

static bool isMp3Name(const String &name) {
  int dot = name.lastIndexOf('.');
  if (dot < 0) return false;
  String ext = name.substring(dot);
  ext.toLowerCase();
  return ext == ".mp3";
}

void listFiles(fs::FS &fs, const char *dirname, uint8_t levels) {
  // Serial.printf("Listing directory: %s\n", dirname);

  File root = fs.open(dirname);
  if (!root) {
    // Serial.printf("Failed to open directory: %s\n", dirname);
    return;
  }
  if (!root.isDirectory()) {
    Serial.println("Not a directory");
    root.close();
    return;
  }

  File file = root.openNextFile();
  while (file && fileCount < MAX_FILES) {
    String name = file.name();
    String full = joinPath(String(dirname), name);
    String base = name;
    int slash = base.lastIndexOf('/');
    if (slash >= 0) base = base.substring(slash + 1);

    if (file.isDirectory()) {
      // Serial.printf("DIR : %s\n", full.c_str());
      if (levels && !base.startsWith(".")) {
        listFiles(fs, full.c_str(), levels - 1);
      }
    } else if (isMp3Name(base)) {
      // Serial.printf("FILE: %s\n", full.c_str());
      audioFiles[fileCount] = full.startsWith("/") ? full : ("/" + full);
      fileCount++;
    }
    file = root.openNextFile();
  }
  root.close();
}

void clearItems(lv_obj_t *roller, const char *new_item) {
  lv_roller_set_options(roller, new_item && new_item[0] ? new_item : " ", LV_ROLLER_MODE_NORMAL);
}

static String displayNameFor(const String &path) {
  String name = path;
  int slash = name.lastIndexOf('/');
  if (slash >= 0) name = name.substring(slash + 1);
  int dot = name.lastIndexOf('.');
  if (dot > 0) name = name.substring(0, dot);
  return name;
}

void add_item_to_roller(lv_obj_t *roller, const char *new_item) {
  const char *current_options = lv_label_get_text(lv_obj_get_child(roller, 0));
  static char options_buffer[5120];
  if (!current_options || !current_options[0] || strcmp(current_options, " ") == 0) {
    snprintf(options_buffer, sizeof(options_buffer), "%s", new_item);
  } else {
    snprintf(options_buffer, sizeof(options_buffer), "%s\n%s", current_options, new_item);
  }
  lv_roller_set_options(roller, options_buffer, LV_ROLLER_MODE_NORMAL);
}

static void fill_song_roller(lv_obj_t *roller) {
  if (fileCount <= 0) {
    lv_roller_set_options(roller, "(no mp3 files)", LV_ROLLER_MODE_NORMAL);
    return;
  }
  static char options_buffer[5120];
  options_buffer[0] = '\0';
  size_t used = 0;
  for (int i = 0; i < fileCount; i++) {
    String label = displayNameFor(audioFiles[i]);
    int written = snprintf(options_buffer + used, sizeof(options_buffer) - used,
                           "%s%s", (used ? "\n" : ""), label.c_str());
    if (written < 0) break;
    used += (size_t)written;
    if (used >= sizeof(options_buffer) - 1) break;
  }
  lv_roller_set_options(roller, options_buffer, LV_ROLLER_MODE_NORMAL);
  lv_roller_set_selected(roller, 0, LV_ANIM_OFF);
}

void setup()
{
  // The native USB CDC RX ring buffer defaults to a small size (well
  // under one YMODEM frame, ~1029 bytes). Diagnostic logging traced a
  // real firmware-level data loss to this: bytes beyond the default
  // buffer's capacity were silently dropped rather than flow-controlled,
  // regardless of how quickly the app-level loop() drained it. Must be
  // set before begin().
  Serial.setRxBufferSize(4096);
  Serial.begin(115200);
  audio_mutex = xSemaphoreCreateMutex();
  pinMode(0, INPUT_PULLUP);
  resetClock();

  // Load preferences
  preferencesManager.begin();
  Volume = preferencesManager.getVolume(10);
  Brightness = preferencesManager.getBrightness(50);

  I2C_Init();
  TCA9554PWR_Init(0x00);
  SD_Init();
  listFiles(SD_MMC, "/", MAX_FILES);
  GifPlayer_ScanFiles();
  Audio_Init();

  // Try to restore last played track, fallback to first track
  String lastTrack = preferencesManager.getLastTrack("");
  if (lastTrack.length() > 0) {
    for (int i = 0; i < fileCount; i++) {
      if (audioFiles[i] == lastTrack) {
        chosenFile = i;
        break;
      }
    }
  } else {
    chosenFile = 0;
  }
  Play_Music_test();

  xTaskCreatePinnedToCore(
    Driver_Loop,
    "Other Driver task",
    20480,
    NULL,
    2,
    NULL,
    0                    // keep LVGL/I2C off the audio core
  );

  // Initialize USB file manager
  USBCommandDispatcher::init();
  printf("[USB] File manager ready\r\n");
}

void changeSong(lv_event_t * e)
{
  if (xSemaphoreTake(audio_mutex, portMAX_DELAY)) {
    chosenFile = lv_roller_get_selected(ui_Roller1);
    xSemaphoreGive(audio_mutex);
  }
}

void toggleBrightnessMode(lv_event_t * e)
{
  if (xSemaphoreTake(audio_mutex, portMAX_DELAY)) {
    unsigned long now = millis();

    // Detect double-tap (clicks within DOUBLE_TAP_THRESHOLD ms)
    if (now - lastTrackPress < DOUBLE_TAP_THRESHOLD) {
      brightnessControlMode = !brightnessControlMode;
      if (brightnessControlMode) {
        // Enter brightness mode
        brightnessActivatedTime = now;
        lv_slider_set_range(ui_Slider1, 0, 100);
        lv_slider_set_value(ui_Slider1, Brightness, LV_ANIM_OFF);
      } else {
        // Exit brightness mode, return to volume
        lv_slider_set_range(ui_Slider1, 0, 21);
        lv_slider_set_value(ui_Slider1, Volume, LV_ANIM_OFF);
        lv_label_set_text(ui_volumeLBL, String(Volume).c_str());
      }
    }
    lastTrackPress = now;
    xSemaphoreGive(audio_mutex);
  }
}

void changeVolume(lv_event_t * e)
{
  if (xSemaphoreTake(audio_mutex, portMAX_DELAY)) {
    int sliderValue = lv_slider_get_value(ui_Slider1);

    if (brightnessControlMode) {
      // In brightness mode, adjust brightness
      changeIsMade = true;
      Brightness = sliderValue;
      lv_label_set_text(ui_volumeLBL, String(Brightness).c_str());
    } else {
      // In volume mode, adjust volume
      changeIsMade = true;
      volumePressed = true;
      Volume = sliderValue;
      lv_label_set_text(ui_volumeLBL, String(Volume).c_str());
    }
    xSemaphoreGive(audio_mutex);
  }
}

static void setPlayButtonPlaying(bool playing) {
  isPlaying = playing;
  if (!ui_Button1) return;
  lv_obj_set_style_bg_color(
    ui_Button1,
    lv_color_hex(playing ? 0xE67E22 : 0x4A9193),
    LV_PART_MAIN | LV_STATE_DEFAULT
  );
}

void playSelected(lv_event_t * e)
{
  if (xSemaphoreTake(audio_mutex, portMAX_DELAY)) {
    // Capture roller selection on the UI thread at press time.
    if (ui_Roller1) chosenFile = lv_roller_get_selected(ui_Roller1);
    changeIsMade = true;
    playPressed = true;
    xSemaphoreGive(audio_mutex);
  }
}

void nextSelected(lv_event_t * e)
{
  if (xSemaphoreTake(audio_mutex, portMAX_DELAY)) {
    changeIsMade = true;
    nextPressed = true;
    xSemaphoreGive(audio_mutex);
  }
}

void prevSelected(lv_event_t * e)
{
  if (xSemaphoreTake(audio_mutex, portMAX_DELAY)) {
    changeIsMade = true;
    prevPressed = true;
    xSemaphoreGive(audio_mutex);
  }
}

void stopSelected(lv_event_t * e)
{
  // Pause button — toggle pause via Audio::pauseResume(), not stopSong()
  if (xSemaphoreTake(audio_mutex, portMAX_DELAY)) {
    changeIsMade = true;
    stopPressed = true;
    xSemaphoreGive(audio_mutex);
  }
}

static int batteryPercent(float volts) {
  // Single-cell LiPo on this board — map 3.30V..4.20V → 0..100%
  const float vmin = 3.30f;
  const float vmax = 4.20f;
  if (volts <= vmin) return 0;
  if (volts >= vmax) return 100;
  return (int)((volts - vmin) * 100.0f / (vmax - vmin) + 0.5f);
}

void Driver_Loop(void *parameter)
{
  I2C_Init();
  TCA9554PWR_Init(0x00);
  delay(20);
  LCD_Init();
  Backlight_Init();
  Set_Backlight(Brightness);
  BAT_Init();
  Lvgl_Init();
  ui_init();
  delay(1000);
  fill_song_roller(ui_Roller1);
  // Initialize volume slider
  lv_slider_set_range(ui_Slider1, 0, 21);
  lv_slider_set_value(ui_Slider1, Volume, LV_ANIM_OFF);
  lv_label_set_text(ui_volumeLBL, String(Volume).c_str());
  // Register brightness mode toggle on MP3 label (double-tap to enter brightness mode)
  lv_obj_add_event_cb(ui_Label8, toggleBrightnessMode, LV_EVENT_PRESSED, NULL);
  // Decorative panels stay clickable by default and sit under controls —
  // disable hit-testing so they cannot steal play/pause/next taps.
  lv_obj_clear_flag(ui_Panel1, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(ui_Panel2, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(ui_Panel3, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(ui_Panel4, LV_OBJ_FLAG_CLICKABLE);
  // Grow hit targets only — do not override SquareLine positions.
  lv_obj_set_ext_click_area(ui_Button1, 16);
  lv_obj_set_ext_click_area(ui_Button2, 24);
  lv_obj_set_ext_click_area(ui_Button3, 16);
  lv_obj_set_ext_click_area(ui_Button4, 16);
  lv_obj_move_foreground(ui_Button1);
  lv_obj_move_foreground(ui_Button2);
  lv_obj_move_foreground(ui_Button3);
  lv_obj_move_foreground(ui_Button4);
  CoverArt_begin(ui_coverart);
  // Keep transport controls above cover art.
  lv_obj_move_foreground(ui_Button1);
  lv_obj_move_foreground(ui_Button2);
  lv_obj_move_foreground(ui_Button3);
  lv_obj_move_foreground(ui_Button4);
  GifPlayer_InitUI();
  // Play startup GIF (loops indefinitely until clicked)
  GifPlayer_PlayStartup("MARTEN.gif");
  setPlayButtonPlaying(true);
  int lastChosen = -1;
  bool lastPlaying = true;
  while (1)
  {
    Lvgl_Loop();
    CoverArt_poll();
    GifPlayer_Poll();

    // Auto-exit brightness mode after 5 seconds of inactivity
    if (brightnessControlMode && brightnessActivatedTime > 0 &&
        millis() - brightnessActivatedTime > BRIGHTNESS_MODE_TIMEOUT) {
      brightnessControlMode = false;
      lv_slider_set_range(ui_Slider1, 0, 21);
      lv_slider_set_value(ui_Slider1, Volume, LV_ANIM_OFF);
      lv_label_set_text(ui_volumeLBL, String(Volume).c_str());
      brightnessActivatedTime = 0;
    }

    if (millis() > batTime + 1000)
    {
      batTime = millis();
      int pct = batteryPercent(BAT_Get_Volts());
      lv_label_set_text(ui_Label1, (String(pct) + "%").c_str());
      if (isPlaying)
        lv_label_set_text(ui_timeLBL, rtc.getTime().substring(3, 8).c_str());
      else
        lv_label_set_text(ui_timeLBL, "00:00");
    }

    if (chosenFile != lastChosen && chosenFile >= 0 && chosenFile < fileCount) {
      lastChosen = chosenFile;
      lv_roller_set_selected(ui_Roller1, chosenFile, LV_ANIM_ON);
    }
    if (isPlaying != lastPlaying) {
      lastPlaying = isPlaying;
      setPlayButtonPlaying(isPlaying);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void loop()
{
  if (xSemaphoreTake(audio_mutex, portMAX_DELAY)) {
    if (changeIsMade == true)
    {
      if (playPressed == 1)
      {
        // Play the roller-selected track. Only resume if it's the same
        // track and currently paused.
        if (!audio.isRunning() && chosenFile == playingIndex) {
          if (!audio.pauseResume()) {
            Play_Music_test();
            resetClock();
          }
        } else {
          Play_Music_test();
          resetClock();
        }
        isPlaying = true;
        playPressed = 0;
      }

      if (stopPressed == true) {
        if (audio.isRunning()) {
          audio.pauseResume();
        }
        isPlaying = audio.isRunning();
        stopPressed = 0;
      }

      if (nextPressed == true) {
        Play_Music_Next();
        resetClock();
        isPlaying = true;
        nextPressed = 0;
      }

      if (prevPressed == true) {
        Play_Music_Prev();
        resetClock();
        isPlaying = true;
        prevPressed = 0;
      }

      if (volumePressed == true) {
        audio.setVolume(Volume);
        preferencesManager.setVolume(Volume);
        volumePressed = 0;
      }

      if (brightnessControlMode && Brightness != LCD_Backlight) {
        Set_Backlight(Brightness);
        preferencesManager.setBrightness(Brightness);
      }

      changeIsMade = false;
    }

    xSemaphoreGive(audio_mutex);
  }

  if (digitalRead(0) == 0)
  {
    if (deb == 0)
    {
      deb = 1;
      chosenFile++;
      if (chosenFile >= fileCount)
        chosenFile = 0;
      Play_Music_test();
      isPlaying = true;
    }
  } else deb = 0;

  audio.loop();

  // Process USB commands and YMODEM data. Debug printf()/Serial.printf() must
  // never be called from this path (or anything it calls) - stdout and
  // Serial share the same USB CDC wire as this binary protocol, and
  // interleaving text into it corrupts every response after it.
  //
  // Cap how many bytes we drain per loop() call so a large YMODEM burst
  // can't starve LVGL/audio - the rest is picked up on the next iteration.
  const uint16_t MAX_USB_BYTES_PER_LOOP = 4096;
  uint16_t usb_bytes_processed = 0;

  while (Serial.available() && usb_bytes_processed < MAX_USB_BYTES_PER_LOOP) {
    uint8_t byte = Serial.read();
    usb_bytes_processed++;
    usb_last_byte_time = millis();

    if (USBCommandDispatcher::isYMODEMActive()) {
      // Forward every byte to the YMODEM handler immediately. It maintains
      // its own frame-assembly buffer across calls, so there is no need to
      // batch here - and batching is actively wrong: a YMODEM frame is
      // 1029 bytes (SOH + block# + ~block# + 1024 data + 2 CRC), never a
      // clean multiple of any fixed local buffer size, so waiting for a
      // full local batch before forwarding can strand the last few bytes
      // of a block indefinitely while the sender is blocked waiting for
      // the ACK that those stranded bytes would have produced.
      USBCommandDispatcher::processData(&byte, 1);
    } else {
      // Normal command mode: wait for complete command
      // Format: [CMD_TYPE(1)] [PAYLOAD_LEN_H(1)] [PAYLOAD_LEN_L(1)] [PAYLOAD(N)]
      if (usb_cmd_buffer_pos >= USB_CMD_BUFFER_SIZE) {
        // Should be unreachable (guarded below once the length is known),
        // but never allow a write past the end of the buffer regardless of
        // how we got here.
        usb_cmd_buffer_pos = 0;
        continue;
      }

      usb_cmd_buffer[usb_cmd_buffer_pos++] = byte;

      // Once the 3-byte header is in (CMD_TYPE + LEN_H + LEN_L), the
      // declared length is known and completeness can be checked on every
      // subsequent byte - including right now, at pos==3, for a
      // zero-payload command like GET_STATS, which never gets another byte
      // to trigger the check afterwards. A separate pos==3-only branch that
      // only validated the length (without also checking completeness)
      // deadlocked exactly that case: the command sat waiting forever for
      // a payload byte that was never coming.
      if (usb_cmd_buffer_pos >= 3) {
        uint16_t payload_len = ((uint16_t)usb_cmd_buffer[1] << 8) | usb_cmd_buffer[2];
        uint16_t expected_len = 3 + payload_len;

        if (usb_cmd_buffer_pos == 3 && 3 + (uint32_t)payload_len > USB_CMD_BUFFER_SIZE) {
          // Declared length can never fit - abort instead of accumulating
          // toward an overflow.
          usb_cmd_buffer_pos = 0;
        } else if (usb_cmd_buffer_pos >= expected_len) {
          usb_cmd_complete = true;
        }
      }
    }

    // Process complete command
    if (usb_cmd_complete) {
      if (xSemaphoreTake(audio_mutex, pdMS_TO_TICKS(100))) {
        USBCommandDispatcher::processCommand(usb_cmd_buffer, usb_cmd_buffer_pos);
        xSemaphoreGive(audio_mutex);
      }
      // If the mutex timed out, the command is silently dropped - the host
      // will time out waiting for a response and can retry. Do not log here.
      usb_cmd_buffer_pos = 0;
      usb_cmd_complete = false;
    }
  }

  // Check for command timeout (incomplete frame that stopped arriving)
  if (usb_cmd_buffer_pos > 0 && (millis() - usb_last_byte_time) > USB_CMD_TIMEOUT_MS && !USBCommandDispatcher::isYMODEMActive()) {
    usb_cmd_buffer_pos = 0;
  }

  // Tick YMODEM handler for timeouts
  USBCommandDispatcher::tick();
}

void audio_eof_mp3(const char *info) {
  if (xSemaphoreTake(audio_mutex, portMAX_DELAY)) {
    resetClock();
    isPlaying = true;
    xSemaphoreGive(audio_mutex);
  }

  Play_Music_Next();
}
