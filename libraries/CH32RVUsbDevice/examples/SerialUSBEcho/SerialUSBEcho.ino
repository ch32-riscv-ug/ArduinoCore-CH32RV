/* Wiring, operation and expected output: README.md / README.ja.md. */
/* SerialUSBEcho - the simplest USB device: a virtual COM port.
 *
 * SerialUSB is predefined, like Serial: SerialUSB.begin() starts the chip's
 * USB with the default identity (1209:0001, "CH32RV Arduino") and one CDC
 * port. Open the port from a terminal and type; every line comes back, and a
 * counter is printed every second while the port is open.
 *
 * Wiring: the board's USB data connector to the PC. See README.md for
 * implemented controllers and the difference between build and hardware checks.
 *
 * Uploading again over USB: opening the port at 1200 baud and closing it (the
 * Arduino "1200 bps touch") on X035/X033 enters the factory USB bootloader
 * (1a86:55e0), where `ch32rv isp` writes it. With no upload it comes back to
 * this sketch after about 10 s. Other series currently reset normally. See
 * README.md and docs/usb.ja.md for upload routes.
 */
#include <CH32RVUsbDevice.h>

void setup() {
  SerialUSB.begin();
}

void loop() {
  static uint32_t last;
  static uint32_t count;

  while (SerialUSB.available()) {
    SerialUSB.write(SerialUSB.read());
  }
  if (SerialUSB && millis() - last >= 1000) {   /* true while a terminal has the port open */
    last = millis();
    SerialUSB.print("count ");
    SerialUSB.println(count++);
  }
}
