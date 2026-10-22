/* Wiring, operation and expected output: README.md / README.ja.md. */
/* CustomDevice - your own identity and several functions in one device.
 *
 * Functions join the device when they are declared, in declaration order:
 * here a CDC port, a DFU runtime interface (re-upload without a serial port)
 * and a vendor HID (63-byte reports under report ID 6). Nothing else of the USB stack is linked. The
 * configuration names the device; begin() lays out the descriptors and
 * connects, and says why when it cannot.
 *
 * USB failures are reported on the hardware UART, since there is no USB to
 * report them on: RX_PIN / TX_PIN below are the chip's default pads for its
 * first USART - change them to your wiring.
 *
 * 1209:0001 is the pid.codes test ID: fine on a bench, not for a product.
 */
#include <CH32RVUsbDevice.h>

static const int RX_PIN = RX;
static const int TX_PIN = TX;

CH32RVUsbDevice usb;                          /* one device per sketch */
CH32RVUsbDeviceCdcSerial console(usb, "Console");   /* a COM port, named */
CH32RVUsbDeviceDfuRuntime dfu(usb);           /* dfu-util -e resets into the bootloader */
CH32RVUsbDeviceHidVendor data(usb);           /* driverless 63-byte reports */

void setup() {
  Serial.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);

  data.onOutputReport([](const CH32RVUsbDeviceHidReport &report) {
    data.sendInput(report.data, report.length);   /* echo it back */
  });

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;
  config.pid = 0x0001;
  config.manufacturer = "Example Labs";
  config.product = "CH32RV custom device";
  config.serialNumber = nullptr;              /* the chip's unique ID */
  config.maxPowerMilliamps = 100;

  if (!usb.begin(config)) {
    Serial.print("USB begin failed: ");
    Serial.println(usb.lastErrorName());      /* e.g. Unsupported on a part without USB support */
    return;
  }
  Serial.println("USB started");
}

void loop() {
  // The callbacks set in setup() run here, and only here.
  deliverEvents();
  while (console.available()) {
    console.write(console.read());
  }
  static bool was;
  if (usb.mounted() != was) {
    was = usb.mounted();
    Serial.println(was ? "host configured the device" : "host gone");
  }
}
