#pragma once
#include "Arduino.h"
#include "FS.h"
#include "SD_MMC.h"
#include "USBProtocol.h"

class DeviceFileManager {
public:
  static bool init();

  // File operations (thread-safe with external mutex)
  static bool listFiles(const char* path, uint8_t depth, uint8_t filter,
                        uint8_t* response_buffer, uint16_t& response_len,
                        uint16_t max_buffer_size);

  static bool deleteFile(const char* path, RespDeleteAck& response);

  static bool getFileInfo(const char* path, RespFileInfo& response);

  static bool createDir(const char* path);

  static bool getStats(RespStats& response);

  // YMODEM support
  static bool prepareForYMODEM(const char* path, uint32_t filesize,
                               bool overwrite, RespYMODEMReady& response);

  static File getYMODEMFile() { return ymodem_file; }
  static void closeYMODEMFile();

private:
  static File ymodem_file;
  static uint32_t ymodem_bytes_written;

  // Path validation - prevent directory traversal
  static bool isValidPath(const char* path);

  // Build binary response for file listing
  static void addFileToBuffer(uint8_t* buffer, uint16_t& offset,
                              const char* name, uint32_t size, bool is_dir);
};
