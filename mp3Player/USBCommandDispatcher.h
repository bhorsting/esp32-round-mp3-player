#pragma once
#include "Arduino.h"
#include "USBProtocol.h"
#include "DeviceFileManager.h"
#include "YMODEMHandler.h"

class USBCommandDispatcher {
public:
  static void init();

  // Process a complete USB command
  static void processCommand(const uint8_t* cmd_data, uint16_t cmd_len);

  // Process USB bulk data (used for YMODEM)
  static void processData(const uint8_t* data, uint16_t len);

  // Tick function - call periodically
  static void tick();

  // Check if YMODEM is active
  static bool isYMODEMActive();

private:
  static YMODEMHandler ymodem;

  // Allocated from PSRAM in init() - large enough that a realistic music
  // library's file listing won't hit the truncation path, while staying
  // safely under 65535 bytes (the wire format's payload length field is
  // only 2 bytes). Living in internal DIRAM as a static array would eat a
  // big chunk of the ~100KB free there, competing with LVGL/audio buffers.
  static const uint32_t RESPONSE_BUFFER_SIZE = 32768;
  static uint8_t* response_buffer;

  // Command handlers - each receives the raw payload and its exact length.
  // Never trust the wire to contain a null terminator for the trailing path
  // field; extractPath() below always bounds it to the bytes actually
  // received and force-terminates it.
  static void handleListFiles(const uint8_t* payload, uint16_t payload_len);
  static void handleDeleteFile(const uint8_t* payload, uint16_t payload_len);
  static void handleYMODEMStart(const uint8_t* payload, uint16_t payload_len);
  static void handleGetStats(const uint8_t* payload, uint16_t payload_len);
  static void handleCreateDir(const uint8_t* payload, uint16_t payload_len);
  static void handleGetFileInfo(const uint8_t* payload, uint16_t payload_len);

  // Safely copy a trailing variable-length path field out of a payload.
  // `path_offset` is where the path starts within the payload (i.e. after
  // any fixed-size header fields). Always null-terminates `out`, clamped to
  // out_size, regardless of what (if anything) was sent on the wire.
  static void extractPath(const uint8_t* payload, uint16_t payload_len,
                           uint16_t path_offset, char* out, size_t out_size);

  // Send response back to host
  static void sendResponse(USBResponse resp_type, const void* payload, uint16_t payload_len);
  static void sendError(uint8_t error_code, const char* message);
};
