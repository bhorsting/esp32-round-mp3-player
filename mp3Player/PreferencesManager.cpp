#include "PreferencesManager.h"

Preferences PreferencesManager::prefs;
bool PreferencesManager::initialized = false;
PreferencesManager preferencesManager;

void PreferencesManager::begin() {
  if (!initialized) {
    prefs.begin("mp3player", false);
    initialized = true;
  }
}

uint8_t PreferencesManager::getVolume(uint8_t defaultVal) {
  begin();
  return prefs.getUChar("volume", defaultVal);
}

void PreferencesManager::setVolume(uint8_t volume) {
  begin();
  prefs.putUChar("volume", volume);
}

uint8_t PreferencesManager::getBrightness(uint8_t defaultVal) {
  begin();
  return prefs.getUChar("brightness", defaultVal);
}

void PreferencesManager::setBrightness(uint8_t brightness) {
  begin();
  prefs.putUChar("brightness", brightness);
}

String PreferencesManager::getLastTrack(const String &defaultVal) {
  begin();
  return prefs.getString("lastTrack", defaultVal);
}

void PreferencesManager::setLastTrack(const String &track) {
  begin();
  prefs.putString("lastTrack", track);
}

void PreferencesManager::clearAll() {
  begin();
  prefs.clear();
}
