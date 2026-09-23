class FileManagerUI {
  constructor(webusb_manager) {
    this.webusb = webusb_manager;
    this.current_path = '/';
    this.files = [];
    this.current_filter = 0;
    this.ymodem = new YMODEMSender(webusb_manager);

    this.setupEventListeners();
  }

  setupEventListeners() {
    // Connection button
    document.getElementById('connectBtn').addEventListener('click', () => this.handleConnect());

    // File list interactions
    document.getElementById('fileList').addEventListener('click', (e) => this.handleFileAction(e));

    // Upload area
    const uploadArea = document.getElementById('uploadArea');
    uploadArea.addEventListener('click', () => document.getElementById('fileInput').click());
    uploadArea.addEventListener('dragover', (e) => {
      e.preventDefault();
      uploadArea.classList.add('dragover');
    });
    uploadArea.addEventListener('dragleave', () => uploadArea.classList.remove('dragover'));
    uploadArea.addEventListener('drop', (e) => {
      e.preventDefault();
      uploadArea.classList.remove('dragover');
      this.handleFilesSelected(e.dataTransfer.files);
    });

    // File input
    document.getElementById('fileInput').addEventListener('change', (e) => {
      this.handleFilesSelected(e.target.files);
    });

    // Buttons
    document.getElementById('refreshStatsBtn').addEventListener('click', () => this.updateStats());
    document.getElementById('createDirBtn').addEventListener('click', () => this.handleCreateDir());

    // Filters
    document.querySelectorAll('input[name="filter"]').forEach(radio => {
      radio.addEventListener('change', (e) => this.handleFilterChange(e.target.value));
    });

    // Breadcrumb
    document.getElementById('breadcrumb').addEventListener('click', (e) => {
      if (e.target.classList.contains('breadcrumb-item')) {
        const path = e.target.dataset.path;
        if (path) this.navigateTo(path);
      }
    });
  }

  async handleConnect() {
    const btn = document.getElementById('connectBtn');
    const status = document.getElementById('connectionStatus');

    if (this.webusb.isConnected()) {
      await this.webusb.disconnect();
      btn.textContent = 'Connect';
      status.classList.remove('connected');
      status.classList.add('disconnected');
      status.querySelector('.status-text').textContent = 'Disconnected';
      this.disableControls();
      this.showNotification('Disconnected', 'info');
    } else {
      try {
        await this.webusb.connect();
        btn.textContent = 'Disconnect';
        status.classList.remove('disconnected');
        status.classList.add('connected');
        status.querySelector('.status-text').textContent = 'Connected';
        this.enableControls();
        this.showNotification('Connected to device', 'success');

        // Load initial file list and stats
        await this.listFiles();
        await this.updateStats();
      } catch (error) {
        this.showNotification(`Connection failed: ${error.message}`, 'error');
        console.error('Connection error:', error);
      }
    }
  }

  // Send a command and wait for a specific response type. If the device
  // instead replies with RESP_ERROR, surfaces its message as a thrown
  // Error rather than a generic "unexpected response type" - every call
  // site gets a real error message for free.
  //
  // A single command can transiently go unanswered - e.g. the device's
  // main loop briefly missed its 100ms mutex window because the touchscreen
  // UI task held it - so a response timeout gets one retry before this
  // gives up and surfaces an error to the user.
  async sendAndExpect(cmd_type, payload, expected_resp_type, _isRetry = false) {
    await this.webusb.sendCommand(cmd_type, payload);

    let resp;
    try {
      resp = await this.webusb.receiveResponse();
    } catch (error) {
      if (!_isRetry && error.message === 'Response timeout') {
        console.warn(`[FileManager] Command 0x${cmd_type.toString(16)} timed out, retrying once`);
        return this.sendAndExpect(cmd_type, payload, expected_resp_type, true);
      }
      throw error;
    }

    const parsed = ProtocolParser.parseResponse(resp);

    if (parsed.type === RESP_ERROR) {
      const err = ProtocolParser.parseError(parsed.payload);
      throw new Error(err.message);
    }
    if (parsed.type !== expected_resp_type) {
      throw new Error(`Unexpected response 0x${parsed.type.toString(16).padStart(2, '0')} ` +
                       `(expected 0x${expected_resp_type.toString(16).padStart(2, '0')})`);
    }
    return parsed.payload;
  }

  async listFiles(path = '/') {
    try {
      this.current_path = path;
      this.updateBreadcrumb();

      const cmd = ProtocolParser.buildListFilesCmd(path, 0, this.current_filter);
      const payload = await this.sendAndExpect(CMD_LIST_FILES, cmd, RESP_LIST_FILES);

      const result = ProtocolParser.parseListFiles(payload);
      this.files = result.files;
      this.renderFileList();
      if (result.truncated) {
        this.showNotification('Directory has more files than fit in one listing - showing a partial list', 'error');
      }
    } catch (error) {
      this.showNotification(`Failed to list files: ${error.message}`, 'error');
      console.error('List error:', error);
    }
  }

  renderFileList() {
    const fileList = document.getElementById('fileList');

    if (this.files.length === 0) {
      fileList.innerHTML = '<div class="file-list-empty">No files</div>';
      return;
    }

    fileList.innerHTML = this.files.map((file, index) => `
      <div class="file-item">
        <div class="file-info">
          <div class="file-icon">${file.is_dir ? '📁' : '📄'}</div>
          <div class="file-details">
            <div class="file-name">${file.name}</div>
            ${!file.is_dir ? `<div class="file-size">${ProtocolParser.formatBytes(file.size)}</div>` : ''}
          </div>
        </div>
        <div class="file-actions">
          ${file.is_dir ?
            `<button class="btn-delete" data-action="open" data-index="${index}">Open</button>` :
            `<button class="btn-delete" data-action="delete" data-index="${index}">Delete</button>`
          }
        </div>
      </div>
    `).join('');
  }

  async handleFileAction(e) {
    const btn = e.target.closest('[data-action]');
    if (!btn) return;

    const action = btn.dataset.action;
    const index = parseInt(btn.dataset.index);
    const file = this.files[index];

    if (action === 'open' && file.is_dir) {
      const new_path = this.current_path === '/' ?
        `/${file.name}` :
        `${this.current_path}/${file.name}`;
      await this.listFiles(new_path);
    } else if (action === 'delete') {
      if (confirm(`Delete "${file.name}"?`)) {
        await this.deleteFile(file.name);
      }
    }
  }

  async deleteFile(filename) {
    try {
      const full_path = this.current_path === '/' ?
        `/${filename}` :
        `${this.current_path}/${filename}`;

      const cmd = ProtocolParser.buildDeleteFileCmd(full_path);
      const payload = await this.sendAndExpect(CMD_DELETE_FILE, cmd, RESP_DELETE_ACK);

      const ack = ProtocolParser.parseDeleteAck(payload);
      if (ack.status === 0) {
        this.showNotification(`Deleted "${filename}"`, 'success');
        await this.listFiles(this.current_path);
      } else {
        throw new Error(ack.message || 'Delete failed');
      }
    } catch (error) {
      this.showNotification(`Delete failed: ${error.message}`, 'error');
      console.error('Delete error:', error);
    }
  }

  async updateStats() {
    try {
      const payload = await this.sendAndExpect(CMD_GET_STATS, new Uint8Array(0), RESP_STATS);
      const stats = ProtocolParser.parseStats(payload);

      document.getElementById('storagePercent').textContent = `${stats.used_percent}%`;
      document.getElementById('storageSize').textContent =
        `${ProtocolParser.formatBytes(stats.used_bytes)} / ${ProtocolParser.formatBytes(stats.total_bytes)}`;
      document.getElementById('storageUsed').style.width = `${stats.used_percent}%`;
    } catch (error) {
      console.error('Stats error:', error);
    }
  }

  updateBreadcrumb() {
    const breadcrumb = document.getElementById('breadcrumb');
    const parts = this.current_path.split('/').filter(p => p);

    let html = '<button class="breadcrumb-item" data-path="/">Root</button>';
    let path = '';

    for (const part of parts) {
      path += '/' + part;
      const is_current = path === this.current_path;
      html += `<button class="breadcrumb-item ${is_current ? 'active' : ''}" data-path="${path}">${part}</button>`;
    }

    breadcrumb.innerHTML = html;
  }

  async handleFilesSelected(files) {
    if (!this.webusb.isConnected()) {
      this.showNotification('Device not connected', 'error');
      return;
    }

    const uploadStatus = document.getElementById('uploadStatus');
    const uploadText = document.getElementById('uploadText');
    const uploadProgressBar = document.getElementById('uploadProgressBar');
    uploadStatus.classList.remove('hidden');

    const file_list = Array.from(files);
    let file_index = 0;

    for (const file of file_list) {
      file_index++;
      try {
        // Never send the browser's raw filename to the device - accents,
        // punctuation outside a safe ASCII set, and very long names have
        // all caused real problems on FAT/SD_MMC in practice, independent
        // of whatever the device-side path validation itself tolerates.
        const safe_name = ProtocolParser.sanitizeFilename(file.name);
        if (safe_name !== file.name) {
          this.showNotification(`Renamed "${file.name}" to "${safe_name}" for device compatibility`, 'info');
        }

        const device_path = this.current_path === '/' ?
          `/${safe_name}` :
          `${this.current_path}/${safe_name}`;

        const prefix = file_list.length > 1 ? `[${file_index}/${file_list.length}] ` : '';
        uploadText.textContent = `${prefix}Uploading ${safe_name}... 0%`;
        uploadProgressBar.style.width = '0%';

        await this.ymodem.sendFile(file, device_path, true, (progress) => {
          const percent = Math.round(progress.percent);
          uploadProgressBar.style.width = `${percent}%`;
          uploadText.textContent = `${prefix}Uploading ${safe_name}... ${percent}% ` +
            `(${ProtocolParser.formatBytes(progress.bytes_sent)} / ${ProtocolParser.formatBytes(progress.total_bytes)})`;
        });

        uploadProgressBar.style.width = '100%';
        uploadText.textContent = `${prefix}Uploaded ${safe_name}`;
        this.showNotification(`Uploaded "${safe_name}"`, 'success');
      } catch (error) {
        uploadText.textContent = `Failed: ${file.name}`;
        this.showNotification(`Upload failed: ${error.message}`, 'error');
        console.error('Upload error:', error);
      }
    }

    uploadStatus.classList.add('hidden');
    document.getElementById('fileInput').value = '';
    await this.listFiles(this.current_path);
    await this.updateStats();
  }

  async handleFilterChange(filter_value) {
    this.current_filter = parseInt(filter_value);
    await this.listFiles(this.current_path);
  }

  async handleCreateDir() {
    const dirname = prompt('Directory name:');
    if (!dirname) return;

    try {
      const new_path = this.current_path === '/' ?
        `/${dirname}` :
        `${this.current_path}/${dirname}`;

      const cmd = ProtocolParser.buildCreateDirCmd(new_path);
      await this.sendAndExpect(CMD_CREATE_DIR, cmd, RESP_DIR_ACK);

      this.showNotification(`Created directory "${dirname}"`, 'success');
      await this.listFiles(this.current_path);
    } catch (error) {
      this.showNotification(`Create directory failed: ${error.message}`, 'error');
      console.error('Create dir error:', error);
    }
  }

  async navigateTo(path) {
    await this.listFiles(path);
  }

  showNotification(message, type = 'info') {
    const notifications = document.getElementById('notifications');
    const notif = document.createElement('div');
    notif.className = `notification ${type}`;
    notif.textContent = message;
    notifications.appendChild(notif);

    setTimeout(() => {
      notif.remove();
    }, 4000);
  }

  enableControls() {
    document.getElementById('refreshStatsBtn').disabled = false;
    document.getElementById('createDirBtn').disabled = false;
    document.getElementById('uploadArea').style.cursor = 'pointer';
  }

  disableControls() {
    document.getElementById('refreshStatsBtn').disabled = true;
    document.getElementById('createDirBtn').disabled = true;
    document.getElementById('uploadArea').style.cursor = 'default';
    document.getElementById('fileList').innerHTML = '<div class="file-list-empty">Connect to device to see files</div>';
  }
}
