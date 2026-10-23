// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。
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
  // en: Store peer serial settings independently of the diagnostic UART. / ja: ログ用UARTとは独立して、対向シリアルの設定を保持する。
  port.begin(115200);
  if (!usb.begin()) {
    Serial.print("usb.begin() failed: ");
    Serial.println(usb.lastErrorName());
  }
}

void loop() {
  // en: Deliver notifications here. / ja: 通知callbackはここで実行する。
  deliverEvents();
  while (port.available()) {
    Serial.write(port.read());
  }
  while (Serial.available()) {
    // en: A forwarding application must retain bytes when write() accepts fewer.
    // ja: 中継へ応用するときは、write()が受け付けなかった分を保持する。
    port.write(Serial.read());
  }
}
