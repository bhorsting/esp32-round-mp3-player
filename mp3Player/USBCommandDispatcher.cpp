#include "USBCommandDispatcher.h"

// Static member initialization
YMODEMHandler USBCommandDispatcher::ymodem;
uint8_t* USBCommandDispatcher::response_buffer = nullptr;

void USBCommandDispatcher::init() {
  DeviceFileManager::init();
  response_buffer = (uint8_t*)ps_malloc(RESPONSE_BUFFER_SIZE);
  // ps_malloc() only succeeds if BOARD_HAS_PSRAM is set and PSRAM actually
  // initialized - fall back to internal RAM rather than leaving a null
  // buffer that would crash the first time a listing is requested.
  if (!response_buffer) {
    response_buffer = (uint8_t*)malloc(RESPONSE_BUFFER_SIZE);
  }
}

void USBCommandDispatcher::sendResponse(USBResponse resp_type, const void* payload, uint16_t payload_len) {
  // Send response header (type + length) - [TYPE] [LEN_H] [LEN_L]
  Serial.write(resp_type);
  Serial.write((uint8_t)((payload_len >> 8) & 0xFF));  // High byte
  Serial.write((uint8_t)(payload_len & 0xFF));        // Low byte

  // Send payload
  if (payload && payload_len > 0) {
    Serial.write((const uint8_t*)payload, payload_len);
  }

  Serial.flush();
}

void USBCommandDispatcher::sendError(uint8_t error_code, const char* message) {
  RespError resp;
  resp.error_code = error_code;
  if (message) {
    strncpy(resp.message, message, sizeof(resp.message) - 1);
    resp.message[sizeof(resp.message) - 1] = '\0';
  } else {
    resp.message[0] = '\0';
  }

  uint16_t payload_len = 1 + strlen(resp.message) + 1;
  sendResponse(RESP_ERROR, &resp, payload_len);
}

void USBCommandDispatcher::extractPath(const uint8_t* payload, uint16_t payload_len,
                                        uint16_t path_offset, char* out, size_t out_size) {
  size_t avail = (payload_len > path_offset) ? (payload_len - path_offset) : 0;
  // Stop early if the sender did include a null terminator, but never read
  // past `avail` - the wire is not trusted to have one.
  size_t path_len = 0;
  while (path_len < avail && payload[path_offset + path_len] != '\0') {
    path_len++;
  }
  if (path_len > out_size - 1) path_len = out_size - 1;
  memcpy(out, payload + path_offset, path_len);
  out[path_len] = '\0';
}

void USBCommandDispatcher::handleListFiles(const uint8_t* payload, uint16_t payload_len) {
  if (payload_len < 2) {
    sendError(255, "Malformed LIST_FILES command");
    return;
  }
  uint8_t depth = payload[0];
  uint8_t filter = payload[1];
  char path[256];
  extractPath(payload, payload_len, 2, path, sizeof(path));

  uint16_t response_len = 0;
  if (DeviceFileManager::listFiles(path, depth, filter,
                                    response_buffer, response_len, RESPONSE_BUFFER_SIZE)) {
    sendResponse(RESP_LIST_FILES, response_buffer, response_len);
  } else {
    sendError(1, "Failed to list files");
  }
}

void USBCommandDispatcher::handleDeleteFile(const uint8_t* payload, uint16_t payload_len) {
  if (payload_len < 1) {
    sendError(255, "Malformed DELETE_FILE command");
    return;
  }
  char path[256];
  extractPath(payload, payload_len, 1, path, sizeof(path));

  RespDeleteAck resp;
  DeviceFileManager::deleteFile(path, resp);
  uint16_t resp_len = 1 + strlen(resp.message) + 1;
  sendResponse(RESP_DELETE_ACK, &resp, resp_len);
}

void USBCommandDispatcher::handleYMODEMStart(const uint8_t* payload, uint16_t payload_len) {
  if (payload_len < 5) {
    sendError(255, "Malformed YMODEM_START command");
    return;
  }
  uint32_t filesize = (uint32_t)payload[0] | ((uint32_t)payload[1] << 8) |
                       ((uint32_t)payload[2] << 16) | ((uint32_t)payload[3] << 24);
  bool overwrite = payload[4] != 0;
  char path[256];
  extractPath(payload, payload_len, 5, path, sizeof(path));

  RespYMODEMReady resp;
  if (DeviceFileManager::prepareForYMODEM(path, filesize, overwrite, resp)) {
    ymodem.startReceive();
  }
  uint16_t resp_len = 1 + strlen(resp.message) + 1;
  sendResponse(RESP_YMODEM_READY, &resp, resp_len);
}

void USBCommandDispatcher::handleGetStats(const uint8_t* payload, uint16_t payload_len) {
  RespStats resp;
  DeviceFileManager::getStats(resp);
  sendResponse(RESP_STATS, &resp, sizeof(RespStats));
}

void USBCommandDispatcher::handleCreateDir(const uint8_t* payload, uint16_t payload_len) {
  if (payload_len < 1) {
    sendError(255, "Malformed CREATE_DIR command");
    return;
  }
  char path[256];
  extractPath(payload, payload_len, 0, path, sizeof(path));

  if (DeviceFileManager::createDir(path)) {
    sendResponse(RESP_DIR_ACK, nullptr, 0);
  } else {
    sendError(2, "Failed to create directory");
  }
}

void USBCommandDispatcher::handleGetFileInfo(const uint8_t* payload, uint16_t payload_len) {
  if (payload_len < 1) {
    sendError(255, "Malformed GET_FILE_INFO command");
    return;
  }
  char path[256];
  extractPath(payload, payload_len, 0, path, sizeof(path));

  RespFileInfo resp;
  if (DeviceFileManager::getFileInfo(path, resp)) {
    sendResponse(RESP_FILE_INFO, &resp, sizeof(RespFileInfo));
  } else {
    sendError(3, "File not found");
  }
}

void USBCommandDispatcher::processCommand(const uint8_t* cmd_data, uint16_t cmd_len) {
  if (cmd_len < 3) return;

  // Frame format: [CMD_TYPE(1)] [LEN_H(1)] [LEN_L(1)] [PAYLOAD(N)]
  USBCommand cmd_type = (USBCommand)cmd_data[0];
  const uint8_t* payload = &cmd_data[3];
  uint16_t payload_len = cmd_len - 3;

  switch (cmd_type) {
    case CMD_LIST_FILES:
      handleListFiles(payload, payload_len);
      break;

    case CMD_DELETE_FILE:
      handleDeleteFile(payload, payload_len);
      break;

    case CMD_YMODEM_START:
      handleYMODEMStart(payload, payload_len);
      break;

    case CMD_GET_STATS:
      handleGetStats(payload, payload_len);
      break;

    case CMD_CREATE_DIR:
      handleCreateDir(payload, payload_len);
      break;

    case CMD_GET_FILE_INFO:
      handleGetFileInfo(payload, payload_len);
      break;


    default:
      sendError(254, "Unknown command");
      break;
  }
}

void USBCommandDispatcher::processData(const uint8_t* data, uint16_t len) {
  if (ymodem.getState() != YMODEM_IDLE) {
    ymodem.processData(data, len);
  }
}

void USBCommandDispatcher::tick() {
  ymodem.tick();
}

bool USBCommandDispatcher::isYMODEMActive() {
  // Deliberately an allow-list of the states that actually consume raw
  // byte-stream data, rather than "anything but IDLE". YMODEM_ERROR is
  // "not idle" but also not receiving anything - if this were `!= IDLE`,
  // any YMODEM failure (bad CRC exhausting retries, a stalled transfer
  // hitting its timeout) would permanently route all subsequent serial
  // bytes - including a fresh command - into the dead transfer's data
  // path instead of back to normal command parsing, deafening the device
  // until reboot.
  YMODEMState state = ymodem.getState();
  return state == YMODEM_RECEIVING_BLOCKS ||
         state == YMODEM_FINALIZING;
}
