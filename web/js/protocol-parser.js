// USBProtocol response types
const RESP_LIST_FILES = 0x81;
const RESP_DELETE_ACK = 0x82;
const RESP_YMODEM_READY = 0x83;
const RESP_STATS = 0x84;
const RESP_DIR_ACK = 0x85;
const RESP_FILE_INFO = 0x86;
const RESP_ERROR = 0x90;

// USBProtocol command types
const CMD_LIST_FILES = 0x01;
const CMD_DELETE_FILE = 0x02;
const CMD_YMODEM_START = 0x03;
const CMD_GET_STATS = 0x04;
const CMD_CREATE_DIR = 0x05;
const CMD_GET_FILE_INFO = 0x06;

class ProtocolParser {
  static parseResponse(data) {
    if (!data || data.length < 3) {
      throw new Error('Invalid response: too short');
    }

    const resp_type = data[0];
    const payload_len = (data[1] << 8) | data[2];
    const payload = data.slice(3, 3 + payload_len);

    return {
      type: resp_type,
      payload: payload,
      toString() {
        return `Response(0x${this.type.toString(16)}, len=${this.payload.length})`;
      }
    };
  }

  static parseListFiles(payload) {
    if (payload.length < 3) {
      throw new Error('Invalid list response');
    }

    // Little-endian, like every other multi-byte field in this payload
    // (the response header itself is the only big-endian exception).
    const file_count = payload[0] | (payload[1] << 8);
    const truncated = payload[2] !== 0;
    const files = [];
    let offset = 3;

    for (let i = 0; i < file_count; i++) {
      // Every record is at least flags(1) + size(4) + name_len(1) = 6 bytes
      // before the name itself. If that doesn't fit, the response is
      // truncated/corrupt - stop instead of reading garbage into the UI.
      if (offset + 6 > payload.length) {
        console.error(`[ProtocolParser] Truncated file list: expected ${file_count} entries, got ${files.length}`);
        break;
      }

      const flags = payload[offset++];
      const is_dir = (flags & 0x01) !== 0;

      const size = (payload[offset] << 0) |
                   (payload[offset + 1] << 8) |
                   (payload[offset + 2] << 16) |
                   (payload[offset + 3] << 24);
      offset += 4;

      const name_len = payload[offset++];
      if (offset + name_len > payload.length) {
        console.error(`[ProtocolParser] Truncated file name at entry ${i}`);
        break;
      }
      const name = new TextDecoder().decode(payload.slice(offset, offset + name_len));
      offset += name_len;

      files.push({
        name: name,
        size: size,
        is_dir: is_dir
      });
    }

    return { count: file_count, files: files, truncated: truncated };
  }

  // Decode a trailing null-terminated message field. The device always
  // sends the terminator, but this must never read past it (or past the
  // payload) even if it didn't - so decode defensively and truncate at the
  // first NUL either way.
  static decodeMessage(bytes) {
    if (!bytes || bytes.length === 0) return '';
    let end = bytes.indexOf(0);
    if (end === -1) end = bytes.length;
    return new TextDecoder().decode(bytes.slice(0, end));
  }

  static parseDeleteAck(payload) {
    if (payload.length < 1) {
      throw new Error('Invalid delete response');
    }

    return {
      status: payload[0],  // 0=success, 1=not_found, 2=access_denied, 3=error
      message: this.decodeMessage(payload.slice(1))
    };
  }

  static parseYMODEMReady(payload) {
    if (payload.length < 1) {
      throw new Error('Invalid YMODEM response');
    }

    return {
      status: payload[0],  // 0=ready, 1=path_error, 2=disk_full, 3=error
      message: this.decodeMessage(payload.slice(1))
    };
  }

  static parseStats(payload) {
    if (payload.length < 24) {
      throw new Error('Invalid stats response');
    }

    const total_bytes = this.readUint64LE(payload, 0);
    const used_bytes = this.readUint64LE(payload, 8);
    const free_bytes = this.readUint64LE(payload, 16);

    return {
      total_bytes: total_bytes,
      used_bytes: used_bytes,
      free_bytes: free_bytes,
      used_percent: Math.round((used_bytes / total_bytes) * 100)
    };
  }

  static parseError(payload) {
    if (payload.length < 1) {
      return { code: 0, message: 'Unknown error' };
    }

    return {
      code: payload[0],
      message: this.decodeMessage(payload.slice(1)) || 'Unknown error'
    };
  }

  // Command builders.
  //
  // Every path field below is explicitly null-terminated. The device-side
  // parser is written to bound itself to the declared payload length
  // regardless (it does not trust the wire), but the terminator is still
  // sent so a byte-for-byte capture of the wire format is self-describing
  // and matches the struct layout on the device.
  static buildListFilesCmd(path = '/', depth = 0, filter = 0) {
    const path_bytes = new TextEncoder().encode(path);
    const cmd = new Uint8Array(2 + path_bytes.length + 1);
    cmd[0] = depth;
    cmd[1] = filter;
    cmd.set(path_bytes, 2);
    cmd[2 + path_bytes.length] = 0;
    return cmd;
  }

  static buildDeleteFileCmd(path) {
    const path_bytes = new TextEncoder().encode(path);
    const cmd = new Uint8Array(1 + path_bytes.length + 1);
    cmd[0] = 0;  // flags
    cmd.set(path_bytes, 1);
    cmd[1 + path_bytes.length] = 0;
    return cmd;
  }

  static buildYMODEMStartCmd(path, filesize, overwrite = false) {
    const path_bytes = new TextEncoder().encode(path);
    const cmd = new Uint8Array(5 + path_bytes.length + 1);

    // Filesize (little-endian)
    cmd[0] = (filesize >> 0) & 0xFF;
    cmd[1] = (filesize >> 8) & 0xFF;
    cmd[2] = (filesize >> 16) & 0xFF;
    cmd[3] = (filesize >> 24) & 0xFF;

    cmd[4] = overwrite ? 1 : 0;
    cmd.set(path_bytes, 5);
    cmd[5 + path_bytes.length] = 0;
    return cmd;
  }

  static buildCreateDirCmd(path) {
    const path_bytes = new TextEncoder().encode(path);
    const cmd = new Uint8Array(path_bytes.length + 1);
    cmd.set(path_bytes, 0);
    cmd[path_bytes.length] = 0;
    return cmd;
  }

  static buildGetFileInfoCmd(path) {
    const path_bytes = new TextEncoder().encode(path);
    const cmd = new Uint8Array(path_bytes.length + 1);
    cmd.set(path_bytes, 0);
    cmd[path_bytes.length] = 0;
    return cmd;
  }

  // Utility
  static readUint64LE(data, offset) {
    // JavaScript can't handle 64-bit integers perfectly, but this is close enough
    const low = (data[offset] << 0) | (data[offset + 1] << 8) |
                (data[offset + 2] << 16) | (data[offset + 3] << 24);
    const high = (data[offset + 4] << 0) | (data[offset + 5] << 8) |
                 (data[offset + 6] << 16) | (data[offset + 7] << 24);
    return (high >>> 0) * 0x100000000 + (low >>> 0);
  }

  // Sanitize a filename before it's ever sent to the device. The device's
  // FAT/SD_MMC filesystem can be finicky about anything outside a
  // conservative ASCII set (case folding, codepages, VFAT long-name
  // quirks), so this strips accents/diacritics down to plain ASCII,
  // replaces anything not in a safe allowlist, and caps the length so it
  // comfortably fits the device's path buffer alongside a directory
  // prefix - independent of whatever the device-side path validation
  // itself accepts.
  static sanitizeFilename(name) {
    if (!name) return 'file';

    // "Concepción" -> "Concepcion", "ROSALÍA" -> "ROSALIA".
    // \p{M} (Unicode "Mark" property, requires the /u flag) covers
    // combining diacritics generally, rather than a hardcoded code point
    // range that's easy to mistype/miscopy.
    const decomposed = name.normalize('NFKD').replace(/\p{M}/gu, '');

    // Split off the extension (by the LAST dot) so truncation never clips
    // it, and so it can be validated/cleaned separately from the base name.
    const dotIndex = decomposed.lastIndexOf('.');
    const hasExt = dotIndex > 0 && dotIndex < decomposed.length - 1;
    let base = hasExt ? decomposed.slice(0, dotIndex) : decomposed;
    let extBody = hasExt ? decomposed.slice(dotIndex + 1) : '';

    const clean = (s) => s
      .replace(/[^a-zA-Z0-9 _\-().]/g, '_')  // disallowed -> underscore
      .replace(/_+/g, '_')                    // collapse runs
      .replace(/^[_\s]+|[_\s]+$/g, '');        // trim stray edges

    base = clean(base).replace(/\.{2,}/g, '.');  // ".." anywhere reads as
                                                   // path traversal to the
                                                   // device's own validator
    extBody = clean(extBody).replace(/\./g, ''); // extension itself shouldn't contain dots

    if (!base || /^\.+$/.test(base)) base = 'file';
    const ext = extBody ? '.' + extBody : '';

    const MAX_BASE_LEN = Math.max(1, 80 - ext.length);
    if (base.length > MAX_BASE_LEN) {
      base = base.slice(0, MAX_BASE_LEN);
    }

    return base + ext;
  }

  static formatBytes(bytes) {
    if (bytes === 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return Math.round((bytes / Math.pow(k, i)) * 100) / 100 + ' ' + sizes[i];
  }
}
