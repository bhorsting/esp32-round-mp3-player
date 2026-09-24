# Quick Start Guide - Brightness & Persistence Features

## What's New

Your MP3 player now has **persistent storage** and **double-tap brightness control**:

### Brightness Control
- **Default**: 50% brightness on first startup
- **Double-tap the volume slider** to toggle between Volume and Brightness control modes
- **Visual feedback**: The label changes from a number to "Brightness" when in brightness mode
- **Range**: 0-100% (displayed on slider)
- **Persists**: Brightness value is saved and restored on power-on

### Volume Control
- Now **persists** across power cycles
- **Default**: 10 (if not previously set)
- **Range**: 0-21 (standard Audio library range)
- **Single tap/drag**: Adjust volume normally

### Track Persistence
- **Last played track** is automatically saved
- **On boot**: Resumes the last track you were playing
- **Fallback**: If the track file is deleted, it starts from the first track
- **Full path** is stored for accurate restoration

## User Manual

### Normal Volume Control
1. **Single tap or drag** the slider
2. Volume changes immediately
3. Volume is automatically saved to flash

### Switch to Brightness Control
1. **Double-tap the slider** quickly (within 500ms)
2. Label changes to "Brightness"
3. Slider range changes to 0-100
4. Slider shows current brightness value

### Adjust Brightness
1. While in brightness mode, **single tap or drag** the slider
2. Brightness changes immediately
3. Brightness is automatically saved to flash

### Return to Volume Control
1. **Double-tap the slider** again (within 500ms)
2. Label changes back to volume display
3. Slider range changes back to 0-21
4. Ready to adjust volume

### Track Playback
1. Play any track from the list
2. Track path is automatically saved
3. **Power off the device**
4. **Power on the device**
5. Device resumes playing the same track
6. If track was deleted: starts from first track instead

## Default Settings (First Startup)

```
Volume:     10
Brightness: 50%
Last Track: First track in list
```

## Storage Details

Settings are stored in ESP32's flash memory using the **Preferences library**:
- **Namespace**: "mp3player"
- **Survives**: Power loss, reboots, firmware updates
- **Keys**: "volume", "brightness", "lastTrack"

## Troubleshooting

### Brightness not working
- Make sure you're double-tapping within 500ms
- Label should change to "Brightness"
- Try a faster double-tap

### Slider behavior
- **Quick taps**: Count as a toggle (if within 500ms of last tap)
- **Slow taps**: Count as separate adjustments
- **Held/dragged**: Continuously adjust current mode

### Settings not persisting
- Check device has power to maintain flash memory
- Try reformatting the device (clears all preferences)

### Track not resuming
- If file was moved or deleted, device starts from first track
- Add your favorite tracks to the SD card root or subdirectories

## Compilation Notes

Make sure you have:
```cpp
#include <Preferences.h>  // Built-in ESP32 library
```

The new files compile without additional libraries:
- `PreferencesManager.h` / `PreferencesManager.cpp`
- Modified: `mp3Player.ino`
- Modified: `Display_ST77916.cpp`

## Testing Recommendations

1. **Change volume** → Power off/on → Should restore
2. **Double-tap slider** → Label should change
3. **Adjust brightness** → Power off/on → Should restore
4. **Play different track** → Power off/on → Should resume same track
5. **Delete last track file** → Power on → Should start from first track

Enjoy your enhanced MP3 player!
