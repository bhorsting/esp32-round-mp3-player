#include "SD_Card.h"
#include "Audio.h"
#include "ui.h"
#include "LVGL_Driver.h"
#include "BAT_Driver.h"
#include <ESP32Time.h>

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
bool volumePressed = 0;
bool isPlaying = 1;

int rolerIndex = 0;
int chosenFile = 0;
String playingSong = "";
Audio audio;
uint8_t Volume = 10;

unsigned long batTime = 0;
int deb = 0;

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

  bool ret = audio.connecttoFS(SD_MMC, audioFiles[chosenFile].c_str());
  playingSong = audioFiles[chosenFile];
  int slash = playingSong.lastIndexOf('/');
  if (slash >= 0) playingSong = playingSong.substring(slash + 1);
  int dot = playingSong.lastIndexOf('.');
  if (dot > 0) playingSong = playingSong.substring(0, dot);

  if (ret)
    printf("Music Read OK: %s\r\n", audioFiles[chosenFile].c_str());
  else
    printf("Music Read Failed: %s\r\n", audioFiles[chosenFile].c_str());
}

void Play_Music_Random() {
  if (fileCount <= 0) {
    printf("Music Read Failed: no mp3 files found on SD\r\n");
    return;
  }
  chosenFile = random(0, fileCount);
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
  Serial.printf("Listing directory: %s\n", dirname);

  File root = fs.open(dirname);
  if (!root) {
    Serial.printf("Failed to open directory: %s\n", dirname);
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
      Serial.printf("DIR : %s\n", full.c_str());
      if (levels && !base.startsWith(".")) {
        listFiles(fs, full.c_str(), levels - 1);
      }
    } else if (isMp3Name(base)) {
      Serial.printf("FILE: %s\n", full.c_str());
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
  Serial.begin(115200);
  audio_mutex = xSemaphoreCreateMutex();
  pinMode(0, INPUT_PULLUP);
  resetClock();

  I2C_Init();
  TCA9554PWR_Init(0x00);
  SD_Init();
  listFiles(SD_MMC, "/", MAX_FILES);
  Audio_Init();
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
}

void changeSong(lv_event_t * e)
{
  if (xSemaphoreTake(audio_mutex, portMAX_DELAY)) {
    chosenFile = lv_roller_get_selected(ui_Roller1);
    xSemaphoreGive(audio_mutex);
  }
}

void changeVolume(lv_event_t * e)
{
  if (xSemaphoreTake(audio_mutex, portMAX_DELAY)) {
    changeIsMade = true;
    volumePressed = true;
    Volume = lv_slider_get_value(ui_Slider1);
    lv_label_set_text(ui_volumeLBL, String(Volume).c_str());
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

void stopSelected(lv_event_t * e)
{
  // Pause button — toggle pause via Audio::pauseResume(), not stopSong()
  if (xSemaphoreTake(audio_mutex, portMAX_DELAY)) {
    changeIsMade = true;
    stopPressed = true;
    xSemaphoreGive(audio_mutex);
  }
}

void Driver_Loop(void *parameter)
{
  I2C_Init();
  TCA9554PWR_Init(0x00);
  delay(20);
  LCD_Init();
  Backlight_Init();
  Set_Backlight(30);
  BAT_Init();
  Lvgl_Init();
  ui_init();
  delay(1000);
  fill_song_roller(ui_Roller1);
  // Decorative panels stay clickable by default and sit under controls —
  // disable hit-testing so they cannot steal play/pause/next taps.
  lv_obj_clear_flag(ui_Panel1, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(ui_Panel2, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(ui_Panel3, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(ui_Panel4, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(ui_Panel5, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_move_foreground(ui_Button1);
  lv_obj_move_foreground(ui_Button2);
  lv_obj_move_foreground(ui_Button3);
  setPlayButtonPlaying(true);
  String lastSong;
  bool lastPlaying = true;
  while (1)
  {
    Lvgl_Loop();

    if (millis() > batTime + 1000)
    {
      batTime = millis();
      float voltage = BAT_Get_Volts();
      lv_label_set_text(ui_Label1, String(voltage).c_str());
      if (isPlaying)
        lv_label_set_text(ui_timeLBL, rtc.getTime().substring(3, 8).c_str());
      else
        lv_label_set_text(ui_timeLBL, "00:00");
    }

    if (playingSong != lastSong) {
      lastSong = playingSong;
      lv_label_set_text(ui_songName, playingSong.c_str());
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
        // Resume if paused; otherwise start the selected track.
        if (audio.isRunning()) {
          Play_Music_test();
          resetClock();
        } else if (!audio.pauseResume()) {
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
        Play_Music_Random();
        resetClock();
        isPlaying = true;
        nextPressed = 0;
      }

      if (volumePressed == true) {
        audio.setVolume(Volume);
        volumePressed = 0;
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
}

void audio_eof_mp3(const char *info) {
  if (xSemaphoreTake(audio_mutex, portMAX_DELAY)) {
    resetClock();
    isPlaying = true;
    xSemaphoreGive(audio_mutex);
  }

  Play_Music_Random();
}
