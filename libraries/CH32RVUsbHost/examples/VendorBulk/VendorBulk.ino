// en: Send UART input to USB vendor bulk OUT and display incoming bulk data.
// ja: UART入力をUSBベンダーbulk OUTへ送り、受信データを表示する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
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
  // en: Start the host controller; connected peripherals are discovered later through USB processing.
  // ja: Host controllerを開始する。接続機器は、その後のUSB処理で検出・列挙される。
  if (!usb.begin()) {
    Serial.print("usb.begin() failed: ");
    Serial.println(usb.lastErrorName());
  }
}

void loop() {
  // en: Run registered notification handlers here; USB transfer processing runs separately.
  // ja: 登録した通知関数をここで実行する。USB転送そのものの処理とは別。
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
  // en: Send only collected bytes; a false result is a transfer failure to inspect with lastErrorName().
  // ja: 集めたバイトだけ送る。falseなら転送失敗なのでlastErrorName()で理由を調べる。
  if (n && !usb.vendorWrite(buf, n)) {
    Serial.print("vendorWrite: ");
    Serial.println(usb.lastErrorName());
  }
}
