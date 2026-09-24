#pragma once
#include <Preferences.h>
#include <Arduino.h>

class PreferencesManager {
private:
  static Preferences prefs;
  static bool initialized;

public:
  static void begin();

  uint8_t getVolume(uint8_t defaultVal = 10);
  void setVolume(uint8_t volume);

  uint8_t getBrightness(uint8_t defaultVal = 50);
  void setBrightness(uint8_t brightness);

  String getLastTrack(const String &defaultVal = "");
  void setLastTrack(const String &track);

  void clearAll();
};

extern PreferencesManager preferencesManager;
