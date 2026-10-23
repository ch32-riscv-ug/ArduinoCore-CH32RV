// en: Bridge the diagnostic UART to a connected USB serial peripheral.
// ja: ログ用UARTと、接続したUSBシリアル機器を双方向に中継する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* requires: USBFS */

#include <CH32RVUsbHost.h>

CH32RVUsbHost usb;
CH32RVUsbHostCdcSerial port(usb);

void setup() {
  Serial.begin(115200);
  usb.onDeviceConnected([](const CH32RVUsbHostDeviceInfo &device) {
    Serial.print("connected: ");
    ch32rvUsbHostPrint(device);
  });
  // en: Store peer serial settings independently of the diagnostic UART.
  // ja: ログ用UARTとは独立して、対向シリアルの設定を保持する。
  port.begin(115200);
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
  // en: Drain bytes from the USB peer into the local UART; the following loop forwards the other way.
  // ja: USBの相手から届いた分をローカルUARTへ流す。次のループは反対方向を中継する。
  while (port.available()) {
    Serial.write(port.read());
  }
  while (Serial.available()) {
    // en: A forwarding application must retain bytes when write() accepts fewer.
    // ja: 中継へ応用するときは、write()が受け付けなかった分を保持する。
    port.write(Serial.read());
  }
}
