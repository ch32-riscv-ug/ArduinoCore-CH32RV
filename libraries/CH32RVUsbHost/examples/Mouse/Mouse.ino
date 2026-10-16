/* Wiring, operation and expected output: README.md / README.ja.md. */
/* Print what a USB mouse does: movement, wheel and buttons. The report layout
 * comes from the mouse's report descriptor, so 16-bit and many-button mice
 * work too. EspUsbHost's EspUsbHostMouse.
 *
 * requires: USBFS */
#include <CH32RVUsbHost.h>

CH32RVUsbHost usb;

void setup() {
  Serial.begin(115200);
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
}
