# MP3 Player Persistence & Brightness Control Implementation

## Overview
Added persistent storage for volume, brightness, and last played track. Implemented double-tap volume slider to control brightness.

## Files Created

### PreferencesManager.h & PreferencesManager.cpp
A new preferences management system using ESP32's NVS (Non-Volatile Storage) via the Preferences library.

**Features:**
- `getVolume(defaultVal=10)` - Load saved volume or use default
- `setVolume(volume)` - Save volume to EEPROM/Flash
- `getBrightness(defaultVal=50)` - Load saved brightness or use default (50%)
- `setBrightness(brightness)` - Save brightness to EEPROM/Flash
- `getLastTrack(defaultVal="")` - Load last played track path
- `setLastTrack(track)` - Save current track path

## Changes to mp3Player.ino

### New Includes
```cpp
#include "PreferencesManager.h"
#include "Display_ST77916.h"
```

### New Global Variables
```cpp
uint8_t Brightness = 50;           // Default brightness: 50%
bool brightnessControlMode = false; // Toggle between volume/brightness control
unsigned long lastVolumePress = 0;  // For double-tap detection
#define DOUBLE_TAP_THRESHOLD 500    // 500ms for double-tap
```

### Startup (setup function)
- Load saved preferences on boot
- Volume defaults to 10 if not saved
- Brightness defaults to 50% if not saved
- Restore last played track; fallback to first track if not found

### Driver_Loop function
- Set backlight to saved brightness value instead of hardcoded 30
- Initialize volume slider with correct range (0-21) and saved value
- Display initial volume on label

### changeVolume function
- Detect double-tap on volume slider (within 500ms)
- Double-tap toggles between Volume and Brightness control modes
- Single tap adjusts the current mode (volume or brightness)
- Visual feedback: label shows "Brightness" when in brightness mode, volume value in volume mode
- Slider range switches: 0-21 for volume, 0-100 for brightness

### Main loop (loop function)
- Save volume to preferences when changed
- Save brightness to preferences when changed
- Apply brightness changes in real-time via Set_Backlight()

### Play_Music_test function
- Save current playing track path to preferences whenever a track loads successfully

## Default Values
- **Volume**: 10 (on first startup)
- **Brightness**: 50% (on first startup)
- **Last Track**: First track in list (on first startup)

## User Interaction

### Double-Tap Volume Slider to Control Brightness
1. **Tap volume slider twice within 500ms** → Enter brightness control mode
   - Label changes to "Brightness"
   - Slider range changes to 0-100
   - Slider position changes to current brightness value
   
2. **Adjust brightness** → Slider shows current brightness (0-100)

3. **Tap volume slider twice within 500ms again** → Return to volume control mode
   - Label changes back to volume display
   - Slider range changes to 0-21
   - Slider position changes to current volume value

4. **Power off/on** → All settings (volume, brightness, last track) are automatically restored

## Technical Details

### Storage
- Uses ESP32's Preferences library (wrapper around NVS)
- Stored in flash memory with namespace "mp3player"
- Keys: "volume", "brightness", "lastTrack"
- Survives power loss and firmware updates

### Brightness Control
- Range: 0-100%
- PWM control via LED driver pin 5
- Changes apply immediately (500ms max latency due to double-tap threshold)

### Volume Control
- Range: 0-21 (Audio library max)
- Changes saved and applied immediately
- Persists across device resets

### Track Persistence
- Full file path stored
- Falls back gracefully if file no longer exists
- Automatically starts from first track if last track unavailable
