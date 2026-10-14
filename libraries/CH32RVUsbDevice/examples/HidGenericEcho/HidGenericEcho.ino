/* HidGenericEcho - a driverless data channel.
 *
 * A vendor-defined HID with 64-byte reports needs no driver on Windows, macOS
 * or Linux, and a program opens it from user space (hidapi, or WebHID in a
 * browser). This sketch answers every output report with the same bytes,
 * the first one incremented.
 *
 * From Python (pip install hidapi):
 *     import hid
 *     d = hid.device(); d.open(0x1209, 0x0001)
 *     d.write(b"\x00" + b"\x01hello")      # report ID 0 first
 *     print(d.read(64, 1000))
 * Linux: the hidraw node needs a udev rule for the VID:PID.
 */
#include <CH32RVUsbDevice.h>

CH32RVUsbDeviceHidGeneric hid(USBDevice, 64);

static uint8_t pending[64];
static volatile bool have;

void setup() {
  hid.onOutputReport([](uint8_t, const uint8_t *data, uint16_t length) {
    memcpy(pending, data, length > sizeof pending ? sizeof pending : length);
    pending[0]++;
    have = true;
  });
  USBDevice.begin();
}

void loop() {
  if (have) {
    have = false;
    hid.send(pending, sizeof pending);       /* sent from loop(), not the callback */
  }
}
