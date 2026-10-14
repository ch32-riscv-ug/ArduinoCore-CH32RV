/* TwoSerialPorts - two virtual COM ports, bridged to each other.
 *
 * How many of a class a device can have is fixed when the USB stack is
 * compiled (one CDC port by default, to keep RAM free on small parts). This
 * sketch's build_opt.h raises it: build_opt.h sits next to the .ino, and each
 * line is passed to every file of the build, the USB stack included.
 *
 *     -DCFG_TUD_CDC=2
 *
 * Without it the second port is refused: begin() still works, the second
 * port's attached() is false and lastError() says TooManyInstances.
 * Each port costs about 0.7 KB of RAM.
 */
#include <CH32RVUsbDevice.h>

CH32RVUsbDeviceCdcSerial portB(USBDevice);

void setup() {
  SerialUSB.setName("Port A");
  portB.setName("Port B");
  SerialUSB.begin();
}

void loop() {
  while (SerialUSB.available()) {
    portB.write(SerialUSB.read());
  }
  while (portB.available()) {
    SerialUSB.write(portB.read());
  }
}
