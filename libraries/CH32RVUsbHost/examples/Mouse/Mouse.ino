// en: Decode USB mouse movement, wheel and button state for UART output.
// ja: USBマウスの移動・ホイール・ボタン状態を読み、UARTへ表示する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* requires: USBFS */

#include <CH32RVUsbHost.h>

CH32RVUsbHost usb;

void setup() {
  Serial.begin(115200);
  // en: Movement is relative; buttons are the current state bitmap.
  // ja: 移動は相対量、buttonsは現在の状態bitmap。
  usb.onMouse([](const CH32RVUsbHostMouseEvent &e) {
    Serial.print("x=");
    Serial.print(e.x);
    Serial.print(" y=");
    Serial.print(e.y);
    Serial.print(" wheel=");
    Serial.print(e.wheel);
    Serial.print(" buttons=0x");
    // en: Print buttons as hexadecimal bits so simultaneous presses remain visible in one value.
    // ja: 同時押しも一つの値で読めるよう、buttonsのbitを16進数で表示する。
    Serial.println(e.buttons, HEX);
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
