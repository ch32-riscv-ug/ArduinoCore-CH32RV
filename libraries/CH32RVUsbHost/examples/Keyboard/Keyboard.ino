// en: Read a USB keyboard and update its Caps Lock and Num Lock LEDs.
// ja: USBキーボードの入力を読み、Caps LockとNum LockのLEDを更新する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* requires: USBFS */

#include <CH32RVUsbHost.h>

CH32RVUsbHost usb;
static bool caps, num;

void setup() {
  Serial.begin(115200);
  // en: Optional JIS character conversion.
  // ja: 任意のJIS文字変換。
  // usb.setKeyboardLayout(CH32RV_USB_HOST_KEYBOARD_LAYOUT_JA_JP);
  usb.onKeyboard([](const CH32RVUsbHostKeyboardEvent &e) {
    // en: Ignore releases for text output; stateful controls must handle them too.
    // ja: 文字表示では解放を無視する。押下状態を扱う用途では解放も処理する。
    if (!e.pressed) {
      return;
    }
    // en: These are HID key identifiers, not ASCII characters; Lock keys also change the stored LED state.
    // ja: これはASCII文字ではなくHIDのキー番号。Lockキーでは保存したLED状態も切り替える。
    if (e.keycode == 0x39) {
      caps = !caps;
      // en: Send LED state back to the keyboard that produced this event.
      // ja: イベントを出したキーボードへLED状態を返す。
      usb.setKeyboardLeds(num, caps, false, e.address);
    } else if (e.keycode == 0x53) {
      num = !num;
      usb.setKeyboardLeds(num, caps, false, e.address);
    } else if (e.ascii == '\r') {
      Serial.println();
    } else if (e.ascii) {
      Serial.print((char)e.ascii);
    }
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
