// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。
/* requires: USBFS */

#include <CH32RVUsbHost.h>

CH32RVUsbHost usb;

void setup() {
  Serial.begin(115200);
  usb.onDeviceConnected([](const CH32RVUsbHostDeviceInfo &device) {
    Serial.println("connected:");
    // en: Use the address from this enumeration, not a fixed address across reconnects.
    // ja: 固定アドレスではなく、今回の列挙通知のaddressを使う。
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
  // en: Deliver notifications here. / ja: 通知callbackはここで実行する。
  deliverEvents();
}
