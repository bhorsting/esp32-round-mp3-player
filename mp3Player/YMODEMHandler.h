#pragma once
#include "Arduino.h"
#include "DeviceFileManager.h"

// YMODEM protocol constants
#define YMODEM_BLOCK_SIZE 1024
#define YMODEM_TIMEOUT_MS 30000  // 30 second timeout
#define YMODEM_MAX_RETRIES 3

// YMODEM control characters
#define YMODEM_SOH 0x01
#define YMODEM_STX 0x02
#define YMODEM_EOT 0x04
#define YMODEM_ACK 0x06
#define YMODEM_NAK 0x15
#define YMODEM_CAN 0x18
#define YMODEM_C 'C'

enum YMODEMState {
  YMODEM_IDLE = 0,
  YMODEM_RECEIVING_BLOCKS,
  YMODEM_FINALIZING,
  YMODEM_ERROR,
};

class YMODEMHandler {
public:
  YMODEMHandler();

  // Initialize YMODEM transfer after YMODEM_START command
  void startReceive();

  // Process received USB data (returns number of bytes consumed)
  uint16_t processData(const uint8_t* data, uint16_t len);

  // Tick function - call periodically to check for timeouts
  void tick();

  // Get current state
  YMODEMState getState() const { return state; }

  // Check if transfer is complete
  bool isComplete() const { return state == YMODEM_IDLE && complete; }

  // Get bytes written
  uint32_t getBytesWritten() const { return bytes_written; }

  // Abort transfer
  void abort();

private:
  YMODEMState state;
  bool complete;

  uint8_t block_number;
  uint8_t retry_count;
  unsigned long last_activity_time;

  // A full frame is SOH(1) + block#(1) + ~block#(1) + data(YMODEM_BLOCK_SIZE)
  // + CRC(2) = YMODEM_BLOCK_SIZE + 5 bytes. This must hold an entire frame,
  // not just the data payload - sizing it to YMODEM_BLOCK_SIZE alone
  // overflows by 5 bytes on every single block.
  static const uint16_t YMODEM_FRAME_SIZE = YMODEM_BLOCK_SIZE + 5;
  uint8_t block_buffer[YMODEM_FRAME_SIZE];
  uint16_t block_buffer_pos;

  uint32_t bytes_written;
  uint32_t total_bytes_expected;

  // CRC16-XMODEM calculation
  static uint16_t crc16_xmodem(const uint8_t* data, uint16_t len);

  // Send response byte to USB
  void sendByte(uint8_t byte);

  // Process a complete YMODEM block
  bool processBlock(uint8_t block_num, uint8_t* data, uint16_t data_len, uint16_t crc_received);

  // Handle reception of control bytes
  void handleControlChar(uint8_t byte);
};
