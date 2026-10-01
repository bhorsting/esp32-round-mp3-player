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

// Best-effort EXIT before the tab closes. beforeunload cannot reliably
// await, but kicking off the write is still better than nothing — the
// device also has a 2-minute USB-idle timeout as a backstop.
window.addEventListener('beforeunload', () => {
  if (file_manager_ui && webusb_manager && webusb_manager.isConnected()) {
    file_manager_ui.leaveUploadMode();
  }
});
