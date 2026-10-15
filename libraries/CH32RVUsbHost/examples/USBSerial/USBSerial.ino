/* A bridge between Serial and a USB serial device plugged into the board -
 * a CDC-ACM device, or an FTDI / CP210x / CH34x / PL2303 adapter.
 * EspUsbHost's EspUsbHostUSBSerial.
 *
 * requires: USBFS */
#include <CH32RVUsbHost.h>

CH32RVUsbHost usb;
CH32RVUsbHostCdcSerial port(usb);

void setup() {
  Serial.begin(115200);
  usb.onDeviceConnected([](const CH32RVUsbHostDeviceInfo &device) {
    Serial.print("connected: ");
    ch32rvUsbHostPrint(device);
  });
  port.begin(115200);
  if (!usb.begin()) {
    Serial.print("usb.begin() failed: ");
    Serial.println(usb.lastErrorName());
  }
}

void loop() {
  while (port.available()) {
    Serial.write(port.read());
  }
  while (Serial.available()) {
    port.write(Serial.read());
  }
}
