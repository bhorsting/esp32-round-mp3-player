# MP3 Player - WebUSB File Manager

A browser-based file manager for the Waveshare ESP32-S3-Touch-LCD-1.85 using WebUSB and YMODEM protocol.

## Features

- **File Browser**: Browse SD card directory structure
- **Upload Files**: Upload MP3 and GIF files using YMODEM protocol
- **Delete Files**: Remove unwanted files from the device
- **Storage Stats**: Real-time storage usage display
- **Dark Theme**: Easy on the eyes UI with responsive design

## Browser Support

WebUSB requires:
- **Chrome 89+**
- **Edge 89+**
- **Opera 75+**

Safari and Firefox do not support WebUSB yet.

## Usage

1. Open `index.html` in a supported browser
2. Click "Connect" to select your device (watch for USB dialog)
3. Browse files and upload/delete as needed

## How It Works

### Architecture

```
Web UI (WebUSB API)
    ↓
Device Firmware (ESP32-S3)
    ├── USB Command Handler
    ├── File Manager (SD_MMC)
    └── YMODEM Handler
    ↓
File System (SD Card)
```

### Protocol

**Commands** (Web → Device):
- `0x01`: List files
- `0x02`: Delete file
- `0x03`: Start YMODEM upload
- `0x04`: Get storage stats
- `0x05`: Create directory
- `0x06`: Get file info

**YMODEM Blocks** (Web → Device):
- 1024-byte data blocks with CRC16-XMODEM
- SOH + block# + ~block# + 1024 bytes + CRC

**Responses** (Device → Web):
- Binary protocol with type + length header
- Errors include descriptive messages

## File Structure

```
web/
├── index.html                 # Main UI
├── css/
│   └── style.css             # Styling (dark theme)
├── js/
│   ├── main.js               # App initialization
│   ├── webusb-manager.js     # WebUSB wrapper
│   ├── protocol-parser.js    # Binary codec + builders
│   ├── ymodem-sender.js      # YMODEM implementation
│   └── file-manager-ui.js    # UI state & interactions
└── README.md                 # This file
```

## Development

To test locally:

```bash
# Python 3
python -m http.server 8000

# Or Node.js
npx http-server
```

Then open `http://localhost:8000` in your browser.

## Known Limitations

- WebUSB dialog appears when connecting (browser security)
- File uploads are limited by USB bandwidth (~40-200 MB/s)
- Large directories (100+ files) may be slow to list
- Filenames limited to 255 characters
- No recursive directory operations

## Troubleshooting

**Device not appearing in USB selector?**
- Check USB cable (data cable required, not charge-only)
- Try a different USB port
- Ensure device is powered on and running firmware

**Upload fails?**
- Check device storage space
- Verify file size is reasonable (test with small file first)
- Try shorter filename

**Disconnects randomly?**
- May be USB driver issue on your OS
- Try different USB port or cable
- Update CH340 drivers if using older Waveshare board

## Notes

- Device firmware must have `ARDUINO_USB_CDC_ON_BOOT=1` enabled
- YMODEM protocol ensures reliable large file transfers with CRC
- All file operations are synchronized with mutex to prevent conflicts with audio playback
