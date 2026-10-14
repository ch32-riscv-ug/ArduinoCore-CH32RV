/* Keyboard - type a line when a button is pressed.
 *
 * A boot keyboard: works in a PC's BIOS too. print() types ASCII text with the
 * US layout; press()/release() take HID usage codes (0x28 = Enter) and
 * modifier bits for shortcuts. leds() is the host's Num/Caps/Scroll Lock.
 *
 * Wiring: a push button from BUTTON_PIN to GND. Nothing is typed until it is
 * pressed - a keyboard that types on its own is a nuisance.
 */
#include <CH32RVUsbDevice.h>

CH32RVUsbDeviceKeyboard keyboard;

static const int BUTTON_PIN = PA1;

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  USBDevice.begin();
}

void loop() {
  static bool was_up = true;
  const bool up = digitalRead(BUTTON_PIN) == HIGH;
  if (was_up && !up && USBDevice.mounted()) {
    keyboard.print("Hello from CH32RV");
    keyboard.press(0x28);                     /* Enter */
    keyboard.releaseAll();
  }
  was_up = up;
  delay(10);                                  /* crude debounce */
}
