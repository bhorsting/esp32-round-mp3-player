#include "YMODEMHandler.h"

// CRC16-XMODEM lookup table (polynomial 0x1021, initial 0x0000)
static const uint16_t crc16_table[256] = {
  0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
  0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
  0x1231, 0x0210, 0x3273, 0x2252, 0x5295, 0x42b4, 0x72d7, 0x62f6,
  0x9339, 0x8318, 0xb37b, 0xa35a, 0xd39d, 0xc3bc, 0xf3df, 0xe3fe,
  0x2462, 0x3443, 0x0420, 0x1401, 0x64c6, 0x74e7, 0x4484, 0x54a5,
  0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ce, 0xf5ef, 0xc58c, 0xd5ad,
  0x3653, 0x2672, 0x1611, 0x0630, 0x76f7, 0x66d6, 0x56b5, 0x4694,
  0xb76b, 0xa74a, 0x9729, 0x8708, 0xf7cf, 0xe7ee, 0xd78d, 0xc7ac,
  0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
  0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
  0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a51, 0x0a70, 0x3a13, 0x2a32,
  0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b59, 0x8b78, 0xbb1b, 0xab3a,
  0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41,
  0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
  0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
  0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
  0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f,
  0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
  0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc31d, 0xd33c, 0xe35f, 0xf37e,
  0x0240, 0x1261, 0x2202, 0x3223, 0x42e4, 0x52c5, 0x62a6, 0x7287,
  0xb2bd, 0xa29c, 0x92ff, 0x82de, 0xf219, 0xe238, 0xd25b, 0xc27a,
  0x5305, 0x4324, 0x7347, 0x6366, 0x53a1, 0x4380, 0x73e3, 0x63c2,
  0xa1a8, 0xb189, 0x81ea, 0x91cb, 0xe10c, 0xf12d, 0xc14e, 0xd16f,
  0x2638, 0x3619, 0x067a, 0x165b, 0x669c, 0x76bd, 0x46de, 0x56ff,
  0xc78f, 0xd7ae, 0xe7cd, 0xf7ec, 0x8729, 0x9708, 0xa76b, 0xb74a,
  0x3b83, 0x2ba2, 0x1bc1, 0x0be0, 0x7b27, 0x6b06, 0x5b65, 0x4b44,
  0xdbbf, 0xcb9e, 0xfbfd, 0xebdc, 0x9b1b, 0x8b3a, 0xbb59, 0xab78,
  0x0bc4, 0x1be5, 0x2b86, 0x3ba7, 0x4b60, 0x5b41, 0x6b22, 0x7b03,
  0xf1ec, 0xe1cd, 0xd1ae, 0xc18f, 0xb148, 0xa169, 0x918a, 0x81ab,
  0x1cdf, 0x0cfe, 0x3c9d, 0x2cbc, 0x5c7b, 0x4c5a, 0x7c39, 0x6c18,
  0xecf7, 0xfcd6, 0xccb5, 0xdc94, 0xac53, 0xbc72, 0x8c11, 0x9c30,
  0x0da1, 0x1d80, 0x2de3, 0x3dc2, 0x4d05, 0x5d24, 0x6d47, 0x7d66,
};

YMODEMHandler::YMODEMHandler()
  : state(YMODEM_IDLE), complete(false), block_number(0), retry_count(0),
    block_buffer_pos(0), bytes_written(0), total_bytes_expected(0) {
  last_activity_time = millis();
}

uint16_t YMODEMHandler::crc16_xmodem(const uint8_t* data, uint16_t len) {
  uint16_t crc = 0x0000;
  for (uint16_t i = 0; i < len; i++) {
    uint8_t tbl_idx = ((crc >> 8) ^ data[i]) & 0xFF;
    crc = ((crc << 8) ^ crc16_table[tbl_idx]) & 0xFFFF;
  }
  return crc;
}

void YMODEMHandler::startReceive() {
  state = YMODEM_RECEIVING_BLOCKS;
  block_number = 0;
  retry_count = 0;
  block_buffer_pos = 0;
  bytes_written = 0;
  complete = false;
  last_activity_time = millis();
}

void YMODEMHandler::sendByte(uint8_t byte) {
  // Send single byte back to host via USB. Must flush explicitly - unlike
  // sendResponse() in USBCommandDispatcher, a single unflushed byte has no
  // guarantee of being pushed out promptly, and the host is actively
  // blocked waiting for exactly this byte (ACK/NAK/CAN) before it will
  // send anything else.
  Serial.write(byte);
  Serial.flush();
}

bool YMODEMHandler::processBlock(uint8_t block_num, uint8_t* data, uint16_t data_len, uint16_t crc_received) {
  // Verify block number
  if (block_num != block_number) {
    retry_count++;
    if (retry_count >= YMODEM_MAX_RETRIES) {
      state = YMODEM_ERROR;
      sendByte(YMODEM_CAN);
      return false;
    }
    sendByte(YMODEM_NAK);
    return false;
  }

  // Verify CRC
  uint16_t calculated_crc = crc16_xmodem(data, data_len);
  if (calculated_crc != crc_received) {
    retry_count++;
    if (retry_count >= YMODEM_MAX_RETRIES) {
      state = YMODEM_ERROR;
      sendByte(YMODEM_CAN);
      return false;
    }
    sendByte(YMODEM_NAK);
    return false;
  }

  // Write to file
  File f = DeviceFileManager::getYMODEMFile();
  if (!f) {
    state = YMODEM_ERROR;
    sendByte(YMODEM_CAN);
    return false;
  }

  size_t written = f.write(data, data_len);
  if (written != data_len) {
    state = YMODEM_ERROR;
    sendByte(YMODEM_CAN);
    return false;
  }

  bytes_written += written;
  block_number++;
  retry_count = 0;
  last_activity_time = millis();

  sendByte(YMODEM_ACK);
  return true;
}

uint16_t YMODEMHandler::processData(const uint8_t* data, uint16_t len) {
  if (state == YMODEM_IDLE || state == YMODEM_ERROR) {
    return 0;
  }

  uint16_t consumed = 0;

  while (consumed < len) {
    uint8_t byte = data[consumed++];

    if (state == YMODEM_RECEIVING_BLOCKS) {
      // Accumulate block data. There is deliberately no separate
      // "waiting for the first block" state: a block is a block, and
      // startReceive() puts us directly into this state.
      block_buffer[block_buffer_pos++] = byte;

      // Check for EOT (end of transmission)
      if (byte == YMODEM_EOT && block_buffer_pos == 1) {
        state = YMODEM_FINALIZING;
        sendByte(YMODEM_ACK);
        DeviceFileManager::closeYMODEMFile();
        complete = true;
        state = YMODEM_IDLE;
        return consumed;
      }

      // Check if we have a complete block
      if (block_buffer_pos >= YMODEM_FRAME_SIZE) {
        uint8_t block_num = block_buffer[1];
        uint8_t block_num_neg = block_buffer[2];
        uint8_t* block_data = &block_buffer[3];
        uint16_t crc_h = block_buffer[3 + YMODEM_BLOCK_SIZE];
        uint16_t crc_l = block_buffer[3 + YMODEM_BLOCK_SIZE + 1];
        uint16_t crc_received = (crc_h << 8) | crc_l;

        // Verify block number complement
        if ((block_num ^ block_num_neg) != 0xFF) {
          retry_count++;
          if (retry_count >= YMODEM_MAX_RETRIES) {
            state = YMODEM_ERROR;
            sendByte(YMODEM_CAN);
          } else {
            sendByte(YMODEM_NAK);
          }
        } else {
          processBlock(block_num, block_data, YMODEM_BLOCK_SIZE, crc_received);
        }

        block_buffer_pos = 0;
      }
    }
  }

  return consumed;
}

void YMODEMHandler::tick() {
  if (state == YMODEM_IDLE || state == YMODEM_ERROR) {
    return;
  }

  unsigned long now = millis();
  if (now - last_activity_time > YMODEM_TIMEOUT_MS) {
    state = YMODEM_ERROR;
    sendByte(YMODEM_CAN);
    DeviceFileManager::closeYMODEMFile();
  }
}

void YMODEMHandler::abort() {
  state = YMODEM_IDLE;
  block_buffer_pos = 0;
  DeviceFileManager::closeYMODEMFile();
}
