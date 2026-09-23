// Main application entry point
let webusb_manager;
let file_manager_ui;

document.addEventListener('DOMContentLoaded', () => {
  // Check WebUSB support
  if (!navigator.usb) {
    console.error('WebUSB not supported');
    const notif = document.createElement('div');
    notif.className = 'notification error';
    notif.textContent = 'WebUSB not supported in this browser. Use Chrome, Edge, or Opera.';
    document.getElementById('notifications').appendChild(notif);
    return;
  }

  // Initialize managers
  webusb_manager = new WebUSBManager();
  file_manager_ui = new FileManagerUI(webusb_manager);

  console.log('[App] Initialized');
});

// Allow reconnecting after disconnect
window.addEventListener('beforeunload', async () => {
  if (webusb_manager && webusb_manager.isConnected()) {
    await webusb_manager.disconnect();
  }
});
