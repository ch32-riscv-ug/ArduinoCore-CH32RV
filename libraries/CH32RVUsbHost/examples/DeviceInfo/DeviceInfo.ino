/* Wiring, operation and expected output: README.md / README.ja.md. */
/* Print every USB device plugged into the board's USB port: identity, strings,
 * interfaces and endpoints. EspUsbHost's EspUsbHostDeviceInfo.
 *
 * The board's own USB port is the host here, so it has to supply VBUS to the
 * device (the chip cannot switch it). On a CH32X035, only the R8T6 / C8T6 /
 * G8U6 / G8R6 have the USB host; begin() reports Unsupported on the others.
 *
 * requires: USBFS */
#include <CH32RVUsbHost.h>

CH32RVUsbHost usb;

void setup() {
  Serial.begin(115200);
  usb.onDeviceConnected([](const CH32RVUsbHostDeviceInfo &device) {
    Serial.println("connected:");
    usb.printDeviceInfo(device.address);
  });
  usb.onDeviceDisconnected([](const CH32RVUsbHostDeviceInfo &device) {
    Serial.print("disconnected: ");
    ch32rvUsbHostPrint(device);
  });
  if (!usb.begin()) {
    Serial.print("usb.begin() failed: ");
    Serial.println(usb.lastErrorName());
  }
}

void loop() {
  // The callbacks set in setup() run here, and only here.
  deliverEvents();
}
