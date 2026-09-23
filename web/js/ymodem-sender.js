// YMODEM protocol constants
const YMODEM_SOH = 0x01;
const YMODEM_EOT = 0x04;
const YMODEM_ACK = 0x06;
const YMODEM_NAK = 0x15;
const YMODEM_CAN = 0x18;
const YMODEM_BLOCK_SIZE = 1024;
const YMODEM_MAX_RETRIES = 3;
const YMODEM_ACK_TIMEOUT_MS = 10000; // SD card writes can be slow

// CRC16-XMODEM lookup table (polynomial 0x1021, initial 0x0000) - must match
// the device's table in YMODEMHandler.cpp exactly, or every block will NAK.
const YMODEM_CRC16_TABLE = [
  0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7, 0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
  0x1231, 0x0210, 0x3273, 0x2252, 0x5295, 0x42b4, 0x72d7, 0x62f6, 0x9339, 0x8318, 0xb37b, 0xa35a, 0xd39d, 0xc3bc, 0xf3df, 0xe3fe,
  0x2462, 0x3443, 0x0420, 0x1401, 0x64c6, 0x74e7, 0x4484, 0x54a5, 0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ce, 0xf5ef, 0xc58c, 0xd5ad,
  0x3653, 0x2672, 0x1611, 0x0630, 0x76f7, 0x66d6, 0x56b5, 0x4694, 0xb76b, 0xa74a, 0x9729, 0x8708, 0xf7cf, 0xe7ee, 0xd78d, 0xc7ac,
  0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823, 0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
  0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a51, 0x0a70, 0x3a13, 0x2a32, 0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b59, 0x8b78, 0xbb1b, 0xab3a,
  0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41, 0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
  0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70, 0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
  0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f, 0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
  0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc31d, 0xd33c, 0xe35f, 0xf37e, 0x0240, 0x1261, 0x2202, 0x3223, 0x42e4, 0x52c5, 0x62a6, 0x7287,
  0xb2bd, 0xa29c, 0x92ff, 0x82de, 0xf219, 0xe238, 0xd25b, 0xc27a, 0x5305, 0x4324, 0x7347, 0x6366, 0x53a1, 0x4380, 0x73e3, 0x63c2,
  0xa1a8, 0xb189, 0x81ea, 0x91cb, 0xe10c, 0xf12d, 0xc14e, 0xd16f, 0x2638, 0x3619, 0x067a, 0x165b, 0x669c, 0x76bd, 0x46de, 0x56ff,
  0xc78f, 0xd7ae, 0xe7cd, 0xf7ec, 0x8729, 0x9708, 0xa76b, 0xb74a, 0x3b83, 0x2ba2, 0x1bc1, 0x0be0, 0x7b27, 0x6b06, 0x5b65, 0x4b44,
  0xdbbf, 0xcb9e, 0xfbfd, 0xebdc, 0x9b1b, 0x8b3a, 0xbb59, 0xab78, 0x0bc4, 0x1be5, 0x2b86, 0x3ba7, 0x4b60, 0x5b41, 0x6b22, 0x7b03,
  0xf1ec, 0xe1cd, 0xd1ae, 0xc18f, 0xb148, 0xa169, 0x918a, 0x81ab, 0x1cdf, 0x0cfe, 0x3c9d, 0x2cbc, 0x5c7b, 0x4c5a, 0x7c39, 0x6c18,
  0xecf7, 0xfcd6, 0xccb5, 0xdc94, 0xac53, 0xbc72, 0x8c11, 0x9c30, 0x0da1, 0x1d80, 0x2de3, 0x3dc2, 0x4d05, 0x5d24, 0x6d47, 0x7d66
];

class YMODEMSender {
  constructor(webusb_manager) {
    this.webusb = webusb_manager;
    this.block_number = 0;
    this.bytes_sent = 0;
    this.total_bytes = 0;
  }

  static crc16xmodem(data) {
    let crc = 0x0000;
    for (let i = 0; i < data.length; i++) {
      const tbl_idx = ((crc >> 8) ^ data[i]) & 0xFF;
      crc = ((crc << 8) ^ YMODEM_CRC16_TABLE[tbl_idx]) & 0xFFFF;
    }
    return crc;
  }

  // Send one block and wait for the device's ACK, retrying on NAK up to
  // YMODEM_MAX_RETRIES times. Throws on CAN or exhausted retries. Without
  // this, a bad block (dropped bytes, CRC mismatch, or the SD card not
  // keeping up) would go uncorrected and the transfer would silently
  // corrupt the file on the device.
  async sendBlockWithRetry(packet) {
    for (let attempt = 0; attempt <= YMODEM_MAX_RETRIES; attempt++) {
      await this.webusb.sendData(packet);

      const reply = await this.webusb.receiveByte(YMODEM_ACK_TIMEOUT_MS);
      if (reply === YMODEM_ACK) {
        return;
      }
      if (reply === YMODEM_CAN) {
        throw new Error('Device aborted transfer (CAN)');
      }
      // NAK or anything else unexpected - retry.
      console.warn(`[YMODEM] Block ${this.block_number} got 0x${reply.toString(16)}, retrying (attempt ${attempt + 1}/${YMODEM_MAX_RETRIES})`);
    }
    throw new Error(`Block ${this.block_number} rejected after ${YMODEM_MAX_RETRIES} retries`);
  }

  // `overwrite` defaults to true: uploading a file with the same name as
  // an existing one (most commonly, retrying a transfer that failed
  // partway through) is the expected path in this UI, not an accidental
  // collision the user needs to be blocked from.
  async sendFile(file, device_path, overwrite = true, onProgress = null) {
    if (!this.webusb.isConnected()) {
      throw new Error('Device not connected');
    }

    this.total_bytes = file.size;
    this.bytes_sent = 0;
    this.block_number = 0;

    try {
      // Step 1: Send YMODEM_START command
      console.log(`[YMODEM] Starting upload: ${file.name} (${file.size} bytes)`);
      const start_cmd = ProtocolParser.buildYMODEMStartCmd(device_path, file.size, overwrite);
      await this.webusb.sendCommand(CMD_YMODEM_START, start_cmd);

      const resp = await this.webusb.receiveResponse();
      const parsed = ProtocolParser.parseResponse(resp);

      if (parsed.type === RESP_ERROR) {
        throw new Error(ProtocolParser.parseError(parsed.payload).message);
      }
      if (parsed.type !== RESP_YMODEM_READY) {
        throw new Error(`Unexpected response 0x${parsed.type.toString(16)}, expected YMODEM_READY`);
      }

      const ready = ProtocolParser.parseYMODEMReady(parsed.payload);
      if (ready.status !== 0) {
        throw new Error(`Device error: ${ready.message}`);
      }

      console.log('[YMODEM] Device ready, starting block transmission');

      // Step 2: Send file in 1024-byte blocks, waiting for each block's ACK
      const file_data = await file.arrayBuffer();
      const view = new Uint8Array(file_data);

      for (let offset = 0; offset < view.length; offset += YMODEM_BLOCK_SIZE) {
        const block_data = new Uint8Array(YMODEM_BLOCK_SIZE);
        const chunk_size = Math.min(YMODEM_BLOCK_SIZE, view.length - offset);

        block_data.set(view.slice(offset, offset + chunk_size), 0);
        if (chunk_size < YMODEM_BLOCK_SIZE) {
          block_data.fill(0, chunk_size);
        }

        const crc = YMODEMSender.crc16xmodem(block_data);

        // Build YMODEM block: [SOH] [BLOCK#] [~BLOCK#] [1024 bytes] [CRC_H] [CRC_L]
        const packet = new Uint8Array(1 + 1 + 1 + YMODEM_BLOCK_SIZE + 2);
        let pos = 0;
        packet[pos++] = YMODEM_SOH;
        packet[pos++] = this.block_number & 0xFF;
        packet[pos++] = (~this.block_number) & 0xFF;
        packet.set(block_data, pos);
        pos += YMODEM_BLOCK_SIZE;
        packet[pos++] = (crc >> 8) & 0xFF;
        packet[pos++] = crc & 0xFF;

        await this.sendBlockWithRetry(packet);

        this.bytes_sent += chunk_size;
        this.block_number++;

        console.log(`[YMODEM] Block ${this.block_number - 1} ACKed (${this.bytes_sent}/${this.total_bytes})`);
        if (onProgress) onProgress(this.getProgress());
      }

      // Step 3: Send EOT and wait for the final ACK
      console.log('[YMODEM] Sending EOT');
      await this.webusb.sendData(new Uint8Array([YMODEM_EOT]));
      const eot_reply = await this.webusb.receiveByte(YMODEM_ACK_TIMEOUT_MS);
      if (eot_reply !== YMODEM_ACK) {
        throw new Error(`Device did not ACK end of transfer (got 0x${eot_reply.toString(16)})`);
      }

      console.log('[YMODEM] Upload complete');
      return true;
    } catch (error) {
      console.error('[YMODEM] Error:', error);
      throw error;
    }
  }

  getProgress() {
    return {
      bytes_sent: this.bytes_sent,
      total_bytes: this.total_bytes,
      percent: this.total_bytes > 0 ? (this.bytes_sent / this.total_bytes) * 100 : 0
    };
  }
}
