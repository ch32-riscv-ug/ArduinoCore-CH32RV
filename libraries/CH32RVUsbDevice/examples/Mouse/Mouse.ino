/* Mouse - draw a small square with the pointer while a button is held.
 *
 * A boot mouse: relative moves, three buttons and a wheel.
 *
 * Wiring: a push button from BUTTON_PIN to GND.
 */
#include <CH32RVUsbDevice.h>

CH32RVUsbDeviceMouse mouse;

static const int BUTTON_PIN = PA1;

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  USBDevice.begin();
}

void loop() {
  static const int8_t steps[4][2] = {{10, 0}, {0, 10}, {-10, 0}, {0, -10}};
  static uint8_t side;
  if (digitalRead(BUTTON_PIN) == LOW && USBDevice.mounted()) {
    for (int i = 0; i < 10; i++) {
      mouse.move(steps[side][0], steps[side][1]);
    }
    side = (side + 1) & 3;
  }
  delay(20);
}
