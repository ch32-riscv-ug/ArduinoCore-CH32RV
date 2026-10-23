// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。
/* requires: USBFS */

#include <CH32RVUsbHost.h>

CH32RVUsbHost usb;
static bool want_open;

void setup() {
  Serial.begin(115200);
  usb.onDeviceConnected([](const CH32RVUsbHostDeviceInfo &) { want_open = true; });
  usb.onVendorData([](const CH32RVUsbHostVendorData &d) {
    Serial.print("in: ");
    ch32rvUsbHostPrintHex(d.data, d.length);
    Serial.println();
  });
  if (!usb.begin()) {
    Serial.print("usb.begin() failed: ");
    Serial.println(usb.lastErrorName());
  }
}

void loop() {
  // en: Deliver notifications here. / ja: 通知callbackはここで実行する。
  deliverEvents();
  // en: Defer interface opening until the loop, after connection notification.
  // ja: 接続通知では予約し、interfaceを開く処理はloop()で行う。
  if (want_open) {
    want_open = false;
    Serial.println(usb.vendorOpen() ? "vendorOpen: ok" : "vendorOpen: no vendor interface");
  }
  uint8_t buf[64];
  size_t n = 0;
  while (Serial.available() && n < sizeof buf) {
    buf[n++] = (uint8_t)Serial.read();
  }
  if (n && !usb.vendorWrite(buf, n)) {
    Serial.print("vendorWrite: ");
    Serial.println(usb.lastErrorName());
  }
}
