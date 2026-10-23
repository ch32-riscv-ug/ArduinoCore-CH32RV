// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。
/* requires: USBFS */

#include <CH32RVUsbHost.h>

CH32RVUsbHost usb;

void setup() {
  Serial.begin(115200);
  // en: Movement is relative; buttons are the current state bitmap. / ja: 移動は相対量、buttonsは現在の状態bitmap。
  usb.onMouse([](const CH32RVUsbHostMouseEvent &e) {
    Serial.print("x=");
    Serial.print(e.x);
    Serial.print(" y=");
    Serial.print(e.y);
    Serial.print(" wheel=");
    Serial.print(e.wheel);
    Serial.print(" buttons=0x");
    Serial.println(e.buttons, HEX);
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
