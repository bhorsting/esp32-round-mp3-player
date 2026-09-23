// Serial Manager using Web Serial API (for USB CDC-ACM devices)
class WebUSBManager {
  constructor() {
    this.port = null;
    this.reader = null;
    this.writer = null;
    this.connected = false;

    // Shared byte queue: both framed command responses and single YMODEM
    // control bytes (ACK/NAK/CAN) arrive on the same stream, so all reads
    // must go through one queue - otherwise a read() call meant for one
    // consumer can silently swallow bytes the other consumer needed.
    this.rx_queue = [];
  }

  async connect() {
    try {
      // Request serial port (USB CDC-ACM)
      this.port = await navigator.serial.requestPort();

      // Open the port at 115200 baud
      await this.port.open({ baudRate: 115200 });

      this.reader = this.port.readable.getReader();
      this.writer = this.port.writable.getWriter();
      this.connected = true;
      this.rx_queue = [];

      console.log('[Serial] Connected to device');
      return true;
    } catch (error) {
      console.error('[Serial] Connection failed:', error);
      this.connected = false;
      throw error;
    }
  }

  async disconnect() {
    if (this.reader) {
      try {
        await this.reader.cancel();
      } catch (e) {}
      this.reader = null;
    }
    if (this.writer) {
      try {
        await this.writer.close();
      } catch (e) {}
      this.writer = null;
    }
    if (this.port) {
      try {
        await this.port.close();
      } catch (e) {}
      this.port = null;
    }
    this.connected = false;
    this.rx_queue = [];
  }

  async sendCommand(cmd_type, payload = null) {
    if (!this.connected || !this.writer) {
      throw new Error('Device not connected');
    }

    const payload_len = payload ? payload.length : 0;
    const cmd_data = new Uint8Array(3 + payload_len);

    cmd_data[0] = cmd_type;
    cmd_data[1] = (payload_len >> 8) & 0xFF;
    cmd_data[2] = payload_len & 0xFF;

    if (payload) {
      cmd_data.set(payload, 3);
    }

    try {
      await this.writer.write(cmd_data);
      console.log(`[Serial] Sent command 0x${cmd_type.toString(16).padStart(2, '0')}, len=${payload_len}`);
    } catch (error) {
      console.error('[Serial] Send failed:', error);
      throw error;
    }
  }

  // Pull more bytes from the port into rx_queue. Returns false on timeout
  // (queue may or may not have grown), throws if the port closes.
  async _fillQueue(timeout_ms) {
    const { value, done } = await Promise.race([
      this.reader.read(),
      new Promise((_, reject) => setTimeout(() => reject(new Error('timeout')), timeout_ms))
    ]);

    if (done || !value) {
      throw new Error('Port closed');
    }

    for (let i = 0; i < value.length; i++) {
      this.rx_queue.push(value[i]);
    }
  }

  // Read exactly one raw byte (used for YMODEM ACK/NAK/CAN control bytes).
  async receiveByte(timeout_ms = 5000) {
    if (!this.connected || !this.reader) {
      throw new Error('Device not connected');
    }

    const start_time = Date.now();
    while (this.rx_queue.length === 0) {
      if (Date.now() - start_time >= timeout_ms) {
        throw new Error('Byte receive timeout');
      }
      try {
        await this._fillQueue(100);
      } catch (e) {
        if (e.message !== 'timeout') throw e;
      }
    }
    return this.rx_queue.shift();
  }

  // Read a complete framed response: [TYPE(1)] [LEN_H(1)] [LEN_L(1)] [PAYLOAD(N)].
  async receiveResponse(timeout_ms = 5000) {
    if (!this.connected || !this.reader) {
      throw new Error('Device not connected');
    }

    const start_time = Date.now();

    while (true) {
      if (this.rx_queue.length >= 3) {
        const payload_len = (this.rx_queue[1] << 8) | this.rx_queue[2];
        const total_expected = 3 + payload_len;

        if (this.rx_queue.length >= total_expected) {
          const response = new Uint8Array(this.rx_queue.slice(0, total_expected));
          this.rx_queue = this.rx_queue.slice(total_expected);
          return response;
        }
      }

      if (Date.now() - start_time >= timeout_ms) {
        console.error(`[Serial] Timeout. Queue has ${this.rx_queue.length} bytes:`, this.rx_queue.slice(0, 20));
        throw new Error('Response timeout');
      }

      try {
        await this._fillQueue(100);
      } catch (e) {
        if (e.message !== 'timeout') throw e;
      }
    }
  }

  async sendData(data) {
    if (!this.connected || !this.writer) {
      throw new Error('Device not connected');
    }

    try {
      await this.writer.write(data);
    } catch (error) {
      console.error('[Serial] Data send failed:', error);
      throw error;
    }
  }

  isConnected() {
    return this.connected;
  }
}
