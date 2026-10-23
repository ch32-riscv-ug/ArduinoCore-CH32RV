// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。
/* requires: USBFS */

#include <CH32RVUsbHost.h>

CH32RVUsbHost usb;
static bool caps, num;

void setup() {
  Serial.begin(115200);
  // en: Optional JIS character conversion. / ja: 任意のJIS文字変換。
  // usb.setKeyboardLayout(CH32RV_USB_HOST_KEYBOARD_LAYOUT_JA_JP);
  usb.onKeyboard([](const CH32RVUsbHostKeyboardEvent &e) {
    // en: Ignore releases for text output; stateful controls must handle them too.
    // ja: 文字表示では解放を無視する。押下状態を扱う用途では解放も処理する。
    if (!e.pressed) {
      return;
    }
    if (e.keycode == 0x39) {
      caps = !caps;
      // en: Send LED state back to the keyboard that produced this event. / ja: イベントを出したキーボードへLED状態を返す。
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
  if (!usb.begin()) {
    Serial.print("usb.begin() failed: ");
    Serial.println(usb.lastErrorName());
  }
}

void loop() {
  // en: Deliver notifications here. / ja: 通知callbackはここで実行する。
  deliverEvents();
}
