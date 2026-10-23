// en: Enumerate connected USB peripherals and print their descriptors.
// ja: 接続したUSB機器を列挙し、記述子の情報を表示する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* requires: USBFS */

#include <CH32RVUsbHost.h>

CH32RVUsbHost usb;

void setup() {
  Serial.begin(115200);
  // en: Register a function for completed device enumeration; it will be called by deliverEvents().
  // ja: 機器の列挙後に呼ぶ関数を登録する。実行されるのはdeliverEvents()の中。
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
}
