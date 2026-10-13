/* Echo what the host sends over the chip's own USB, and print a line every
 * second while a terminal has the port open.
 *
 * Runs on CH32X035/X033 (checked on a WeAct CH32X035F8U6 board); on the
 * other parts SerialUSB never opens - see libraries/TinyUSB/README.md.
 *
 * Opening the port at 1200 baud and closing it resets the chip into its
 * factory USB bootloader (1a86:55e0) for `ch32rv isp flash`; with no upload
 * it comes back to this sketch after about 60 s.
 */
#include <TinyUSB.h>

void setup() {
  SerialUSB.begin();
}

void loop() {
  static uint32_t last;

  while (SerialUSB.available()) {
    SerialUSB.write((uint8_t)SerialUSB.read());
  }
  if (millis() - last >= 1000) {
    last = millis();
    if (SerialUSB) {
      SerialUSB.print("uptime ");
      SerialUSB.println(last);
    }
  }
}
