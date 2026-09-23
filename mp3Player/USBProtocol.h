#pragma once
#include <cstdint>

// WebUSB command structure (sent from host)
enum USBCommand : uint8_t {
  CMD_LIST_FILES = 0x01,
  CMD_DELETE_FILE = 0x02,
  CMD_YMODEM_START = 0x03,
  CMD_GET_STATS = 0x04,
  CMD_CREATE_DIR = 0x05,
  CMD_GET_FILE_INFO = 0x06,
};

// WebUSB response type (sent to device)
enum USBResponse : uint8_t {
  RESP_LIST_FILES = 0x81,
  RESP_DELETE_ACK = 0x82,
  RESP_YMODEM_READY = 0x83,
  RESP_STATS = 0x84,
  RESP_DIR_ACK = 0x85,
  RESP_FILE_INFO = 0x86,
  RESP_ERROR = 0x90,
};

// Command payload structures (packed for USB)
#pragma pack(1)

struct CmdListFiles {
  uint8_t depth;      // 0=current only, 1=recursive
  uint8_t filter;     // 0=all, 1=mp3, 2=gif
  char path[256];     // null-terminated path
};

struct CmdDeleteFile {
  uint8_t flags;      // 0=normal, 1=force
  char path[256];     // null-terminated path
};

struct CmdYMODEMStart {
  uint32_t filesize;  // file size in bytes
  uint8_t overwrite;  // 0=error if exists, 1=overwrite
  char path[256];     // null-terminated target path
};

struct CmdGetStats {
  // no payload
};

struct CmdCreateDir {
  char path[256];     // null-terminated directory path
};

struct CmdGetFileInfo {
  char path[256];     // null-terminated file path
};

// Response payload structures
struct RespListFiles {
  uint16_t file_count;
  uint8_t truncated;  // 1 if the directory had more entries than fit in
                       // this response - the client should tell the user
                       // rather than silently showing a partial list.
  // followed by: [flags(1) | size(4) | name_len(1) | name(N)]...
};

struct RespDeleteAck {
  uint8_t status;     // 0=success, 1=not_found, 2=access_denied, 3=error
  char message[64];   // error message if any
};

struct RespYMODEMReady {
  uint8_t status;     // 0=ready, 1=path_error, 2=disk_full, 3=error
  char message[64];   // error message if any
};

struct RespStats {
  uint64_t total_bytes;
  uint64_t used_bytes;
  uint64_t free_bytes;
};

struct RespFileInfo {
  uint32_t size;
  uint8_t is_dir;
  uint32_t modified_time;  // unix timestamp
};

struct RespError {
  uint8_t error_code;
  char message[128];
};

#pragma pack()

// USB endpoint definitions
#define USB_EP_OUT 0x01  // Host → Device (commands + YMODEM data)
#define USB_EP_IN 0x81   // Device → Host (responses)
