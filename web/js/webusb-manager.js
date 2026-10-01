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

    // At most one reader.read() call may ever be in flight (see
    // _ensureReading() below) - Promise.race cannot cancel its losing
    // side, so racing a fresh read() against a per-iteration timeout and
    // discarding it on timeout orphans that read: it keeps running, and
    // whatever chunk it eventually resolves with is lost because nothing
    // is listening for it anymore. Larger, multi-packet responses (a big
    // directory listing) are exactly when a sub-read is likely to outlast
    // a short timeout, so this reliably drops a chunk in the middle of
    // the response - explains truncated/misaligned reads under load.
    this._pendingReadPromise = null;
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
      this._pendingReadPromise = null;

      // Surface unexpected unplugs so the UI can leave upload mode cleanly.
      this.port.addEventListener('disconnect', () => {
        this.connected = false;
        this.reader = null;
        this.writer = null;
        this.port = null;
        this.rx_queue = [];
        this._pendingReadPromise = null;
        if (typeof this.onUnexpectedDisconnect === 'function') {
          this.onUnexpectedDisconnect();
        }
      });

      console.log('[Serial] Connected to device');

      // Boot-time printf() spam (PWR/SD/GIF/…) sits in the CDC TX buffer and
      // arrives the moment the port opens. Drain it before any binary command
      // so framed responses aren't parsed against leftover ASCII.
      await this.drainRx(400);

      return true;
    } catch (error) {
      console.error('[Serial] Connection failed:', error);
      this.connected = false;
      throw error;
    }
  }

  // Pull and discard whatever is already queued (and anything that arrives
  // within `ms`) so the next receiveResponse() starts on a clean stream.
  async drainRx(ms = 300) {
    if (!this.connected || !this.reader) return;
    const deadline = Date.now() + ms;
    let discarded = 0;
    while (Date.now() < deadline) {
      await this._waitForMoreData(deadline);
      discarded += this.rx_queue.length;
      this.rx_queue = [];
    }
    discarded += this.rx_queue.length;
    this.rx_queue = [];
    if (discarded > 0) {
      console.log(`[Serial] Drained ${discarded} boot/noise byte(s)`);
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
    this._pendingReadPromise = null;
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

  // Returns the single in-flight reader.read() call, starting one if none
  // is outstanding. Callers may race this against a timeout and give up
  // on *waiting* for it, but the read itself is never abandoned - its
  // .then() below always runs and pushes the chunk into rx_queue whether
  // or not anyone was actively waiting when it resolved, so no data is
  // ever lost to an impatient caller moving on.
  _ensureReading() {
    if (!this._pendingReadPromise) {
      const p = this.reader.read().then(({ value, done }) => {
        this._pendingReadPromise = null;
        if (done || !value) {
          throw new Error('Port closed');
        }
        for (let i = 0; i < value.length; i++) {
          this.rx_queue.push(value[i]);
        }
      });
      // Keep console quiet if this rejects while nothing happens to be
      // racing it at that moment - the real rejection still reaches
      // whichever caller *does* await `this._pendingReadPromise` later,
      // since a promise can have multiple independent handlers.
      p.catch(() => {});
      this._pendingReadPromise = p;
    }
    return this._pendingReadPromise;
  }

  // Waits for at least one more chunk to land in rx_queue, or gives up
  // (without losing the in-flight read) once `deadline` (Date.now()-based
  // ms) passes.
  async _waitForMoreData(deadline) {
    const remaining = deadline - Date.now();
    if (remaining <= 0) return;
    // The timeout branch resolves (not rejects) - "gave up waiting" is
    // not an error here, it just means loop back and check rx_queue /
    // the overall deadline again. Only a real read failure (e.g. "Port
    // closed") should propagate as a rejection.
    await Promise.race([
      this._ensureReading(),
      new Promise((resolve) => setTimeout(resolve, Math.min(remaining, 100)))
    ]);
  }

  // Read exactly one raw byte (used for YMODEM ACK/NAK/CAN control bytes).
  async receiveByte(timeout_ms = 5000) {
    if (!this.connected || !this.reader) {
      throw new Error('Device not connected');
    }

    const deadline = Date.now() + timeout_ms;
    while (this.rx_queue.length === 0) {
      if (Date.now() >= deadline) {
        throw new Error('Byte receive timeout');
      }
      await this._waitForMoreData(deadline);
    }
    return this.rx_queue.shift();
  }

  // Read a complete framed response: [TYPE(1)] [LEN_H(1)] [LEN_L(1)] [PAYLOAD(N)].
  async receiveResponse(timeout_ms = 5000) {
    if (!this.connected || !this.reader) {
      throw new Error('Device not connected');
    }

    const deadline = Date.now() + timeout_ms;
    // Valid device responses (see USBProtocol.h). Anything else is leftover
    // printf ASCII or framing junk — skip until we re-sync.
    const isRespType = (b) =>
      (b >= 0x81 && b <= 0x87) || b === 0x90;
    const MAX_PAYLOAD = 32768;

    while (true) {
      // Resync: drop leading bytes that cannot be a response type.
      while (this.rx_queue.length > 0 && !isRespType(this.rx_queue[0])) {
        this.rx_queue.shift();
      }

      if (this.rx_queue.length >= 3) {
        const payload_len = (this.rx_queue[1] << 8) | this.rx_queue[2];
        if (payload_len > MAX_PAYLOAD) {
          // Plausible type byte was actually noise — skip it and resync.
          this.rx_queue.shift();
          continue;
        }
        const total_expected = 3 + payload_len;

        if (this.rx_queue.length >= total_expected) {
          const response = new Uint8Array(this.rx_queue.slice(0, total_expected));
          this.rx_queue = this.rx_queue.slice(total_expected);
          return response;
        }
      }

      if (Date.now() >= deadline) {
        console.error(`[Serial] Timeout. Queue has ${this.rx_queue.length} bytes:`, this.rx_queue.slice(0, 20));
        throw new Error('Response timeout');
      }

      await this._waitForMoreData(deadline);
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
