#include "DeviceFileManager.h"

// Static member initialization
File DeviceFileManager::ymodem_file;
uint32_t DeviceFileManager::ymodem_bytes_written = 0;

bool DeviceFileManager::init() {
  // SD card already initialized in SD_Init()
  // Just verify it's accessible
  File root = SD_MMC.open("/sdcard");
  if (!root) {
    // printf("[FileManager] SD card not accessible\r\n");
    return false;
  }
  root.close();
  // printf("[FileManager] Initialized\r\n");
  return true;
}

bool DeviceFileManager::isValidPath(const char* path) {
  if (!path) return false;
  size_t len = strlen(path);
  if (len == 0 || len > 255) return false;

  // Must start with /
  if (path[0] != '/') return false;

  // Reject parent directory traversal - the only real security boundary.
  // Everything else (spaces, unicode, punctuation) is a legitimate filename
  // character - real music libraries are full of them.
  if (strstr(path, "..") != nullptr) return false;

  // Reject control characters (NUL is already handled by strlen above).
  // Explicitly cast to unsigned char: on this platform char is signed, and
  // UTF-8 continuation bytes have the high bit set, which is undefined
  // behavior for isalnum()/isprint() on a signed char - hence a manual range
  // check here instead of relying on ctype.h.
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)path[i];
    if (c < 0x20) return false;
  }

  return true;
}

void DeviceFileManager::addFileToBuffer(uint8_t* buffer, uint16_t& offset,
                                         const char* name, uint32_t size, bool is_dir) {
  if (!buffer || !name) return;

  uint8_t flags = is_dir ? 0x01 : 0x00;

  buffer[offset++] = flags;

  // Size (little-endian, 4 bytes)
  buffer[offset++] = (size >> 0) & 0xFF;
  buffer[offset++] = (size >> 8) & 0xFF;
  buffer[offset++] = (size >> 16) & 0xFF;
  buffer[offset++] = (size >> 24) & 0xFF;

  // Name length
  uint8_t name_len = strlen(name);
  buffer[offset++] = name_len;

  // Name
  memcpy(&buffer[offset], name, name_len);
  offset += name_len;
}

bool DeviceFileManager::listFiles(const char* path, uint8_t depth, uint8_t filter,
                                   uint8_t* response_buffer, uint16_t& response_len,
                                   uint16_t max_buffer_size) {
  // Serial.printf("[DEBUG] listFiles: path='%s', depth=%d, filter=%d\r\n", path, depth, filter);

  if (!isValidPath(path)) {
    // Serial.printf("[DEBUG] Invalid path validation failed\r\n");
    response_len = 0;
    return false;
  }

  // Serial.printf("[DEBUG] Path validated, opening...\r\n");
  File dir = SD_MMC.open(path);
  if (!dir || !dir.isDirectory()) {
    // Serial.printf("[DEBUG] Failed to open dir or not a directory\r\n");
    dir.close();
    response_len = 0;
    return false;
  }
  // Serial.printf("[DEBUG] Directory opened successfully\r\n");

  // Start building response. file_count is written as two explicit
  // little-endian bytes below (matching every other multi-byte field in
  // this payload) rather than through the RespListFiles struct field - a
  // plain struct assignment is little-endian here only because the CPU
  // happens to be, and that implicit assumption is exactly what drifted
  // out of sync with the JS parser once before.
  uint16_t offset = sizeof(RespListFiles);
  uint16_t file_count = 0;

  File file = dir.openNextFile();
  while (file && offset < max_buffer_size - 256) {  // Leave 256 bytes margin
    const char* fname = file.name();

    // Apply filter
    bool include_file = false;
    if (file.isDirectory()) {
      include_file = true;
    } else {
      if (filter == 0) {
        include_file = true;  // All files
      } else if (filter == 1 && strstr(fname, ".mp3")) {
        include_file = true;
      } else if (filter == 2 && strstr(fname, ".gif")) {
        include_file = true;
      }
    }

    if (include_file) {
      addFileToBuffer(response_buffer, offset, fname, file.size(), file.isDirectory());
      file_count++;
    }

    file = dir.openNextFile();
  }

  // If the loop stopped because we ran out of buffer space rather than
  // because there were no more entries, `file` still holds a valid,
  // not-yet-consumed entry - tell the client so it doesn't silently show
  // a partial listing as if it were complete.
  bool truncated = (bool)file;

  dir.close();

  // Write file count at start of response as explicit little-endian bytes.
  response_buffer[0] = file_count & 0xFF;
  response_buffer[1] = (file_count >> 8) & 0xFF;
  response_buffer[2] = truncated ? 1 : 0;
  response_len = offset;

  // printf("[FileManager] Listed %d files in %s\r\n", file_count, path);
  return true;
}

bool DeviceFileManager::deleteFile(const char* path, RespDeleteAck& response) {
  if (!isValidPath(path)) {
    response.status = 2;  // access_denied
    snprintf(response.message, sizeof(response.message), "Invalid path");
    return false;
  }

  // Check if file exists
  if (!SD_MMC.exists(path)) {
    response.status = 1;  // not_found
    snprintf(response.message, sizeof(response.message), "File not found");
    return false;
  }

  if (SD_MMC.remove(path)) {
    response.status = 0;  // success
    response.message[0] = '\0';
    // printf("[FileManager] Deleted: %s\r\n", path);
    return true;
  } else {
    response.status = 3;  // error
    snprintf(response.message, sizeof(response.message), "Cannot delete file");
    return false;
  }
}

bool DeviceFileManager::getFileInfo(const char* path, RespFileInfo& response) {
  if (!isValidPath(path)) {
    return false;
  }

  File file = SD_MMC.open(path);
  if (!file) {
    return false;
  }

  response.size = file.size();
  response.is_dir = file.isDirectory() ? 1 : 0;
  response.modified_time = 0;  // TODO: get actual modification time if available

  file.close();
  return true;
}

bool DeviceFileManager::createDir(const char* path) {
  if (!isValidPath(path)) {
    // printf("[FileManager] Invalid path for directory creation: %s\r\n", path);
    return false;
  }

  if (SD_MMC.mkdir(path)) {
    // printf("[FileManager] Created directory: %s\r\n", path);
    return true;
  } else {
    // printf("[FileManager] Failed to create directory: %s\r\n", path);
    return false;
  }
}

bool DeviceFileManager::getStats(RespStats& response) {
  response.total_bytes = SD_MMC.totalBytes();
  response.used_bytes = SD_MMC.usedBytes();
  response.free_bytes = response.total_bytes - response.used_bytes;

  // printf("[FileManager] Stats: total=%llu, used=%llu, free=%llu\r\n",
  //        response.total_bytes, response.used_bytes, response.free_bytes);

  return true;
}

bool DeviceFileManager::prepareForYMODEM(const char* path, uint32_t filesize,
                                         bool overwrite, RespYMODEMReady& response) {
  if (!isValidPath(path)) {
    response.status = 1;  // path_error
    snprintf(response.message, sizeof(response.message), "Invalid path");
    return false;
  }

  // Check if file exists and overwrite setting
  if (SD_MMC.exists(path) && !overwrite) {
    response.status = 1;  // path_error
    snprintf(response.message, sizeof(response.message), "File already exists");
    return false;
  }

  // Check free space
  uint64_t free_space = SD_MMC.totalBytes() - SD_MMC.usedBytes();
  if (filesize > free_space) {
    response.status = 2;  // disk_full
    snprintf(response.message, sizeof(response.message), "Insufficient space");
    return false;
  }

  // Delete existing file if overwriting
  if (overwrite && SD_MMC.exists(path)) {
    SD_MMC.remove(path);
  }

  // Try to open file for writing
  ymodem_file = SD_MMC.open(path, FILE_WRITE);
  if (!ymodem_file) {
    response.status = 3;  // error
    snprintf(response.message, sizeof(response.message), "Cannot open file");
    return false;
  }

  ymodem_bytes_written = 0;
  response.status = 0;  // ready
  response.message[0] = '\0';

  // printf("[FileManager] Prepared for YMODEM: %s (size: %lu)\r\n", path, filesize);
  return true;
}

void DeviceFileManager::closeYMODEMFile() {
  if (ymodem_file) {
    ymodem_file.close();
    // printf("[FileManager] Closed YMODEM file after %lu bytes\r\n", ymodem_bytes_written);
  }
  ymodem_bytes_written = 0;
}
