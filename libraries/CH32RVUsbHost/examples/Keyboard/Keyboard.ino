/* Type on a USB keyboard and see the characters on Serial. Caps Lock and Num
 * Lock drive the keyboard's LEDs. EspUsbHost's EspUsbHostKeyboard.
 *
 * requires: USBFS */
#include <CH32RVUsbHost.h>

CH32RVUsbHost usb;
static bool caps, num;

void setup() {
  Serial.begin(115200);
  /* usb.setKeyboardLayout(CH32RV_USB_HOST_KEYBOARD_LAYOUT_JA_JP);  for a JIS keyboard */
  usb.onKeyboard([](const CH32RVUsbHostKeyboardEvent &e) {
    if (!e.pressed) {
      return;
    }
    if (e.keycode == 0x39) {          /* Caps Lock */
      caps = !caps;
      usb.setKeyboardLeds(num, caps, false, e.address);
    } else if (e.keycode == 0x53) {   /* Num Lock */
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
}
