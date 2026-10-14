/* SerialUSBEcho - the simplest USB device: a virtual COM port.
 *
 * SerialUSB is predefined, like Serial: SerialUSB.begin() starts the chip's
 * USB with the default identity (1209:0001, "CH32RV Arduino") and one CDC
 * port. Open the port from a terminal and type; every line comes back, and a
 * counter is printed every second while the port is open.
 *
 * Wiring: the board's USB connector to the PC. Runs on CH32X035/X033 (checked
 * on a WeAct CH32X035F8U6 board); on other series SerialUSB never opens.
 *
 * Uploading again over USB: opening the port at 1200 baud and closing it (the
 * Arduino "1200 bps touch") resets the chip into its factory USB bootloader
 * (1a86:55e0), where `ch32rv isp` writes it. With no upload it comes back to
 * this sketch after about 10 s. See docs/usb.ja.md.
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
