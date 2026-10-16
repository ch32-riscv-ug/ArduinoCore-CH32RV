/* Wiring, operation and expected output: README.md / README.ja.md. */
/* DfuRuntime - re-upload over USB without a serial port.
 *
 * Every device gets a DFU runtime interface by default
 * (CH32RVUsbDeviceConfig::dfuRuntime); declaring one, as here, is only needed
 * to use onDetach().
 *
 * A DFU runtime interface has no endpoints and costs about 0.5 KB of flash
 * and 20 bytes of RAM. A host that sends it DFU_DETACH - `dfu-util -e`, or ch32rv -
 * on X035/X033 resets into the factory USB bootloader (1a86:55e0),
 * where `ch32rv isp` writes the new sketch. Without an upload the bootloader
 * returns to this sketch after about 10 s. Other series currently reset
 * normally instead; see README.md for the supported upload entry.
 *
 * Add it to any device - it does not need a CDC port, which the 1200 bps
 * touch does. onDetach() can refuse, e.g. while a motor is running.
 *
 * Linux: dfu-util needs access to the device (a udev rule for its VID:PID).
 * Windows: the interface carries a Microsoft OS 2.0 descriptor naming WinUSB,
 * so dfu-util opens it without a driver install (not yet tried on Windows).
 */
#include <CH32RVUsbDevice.h>

CH32RVUsbDeviceDfuRuntime dfu;                /* on the predefined USBDevice */

static const int BUSY_PIN = PA1;              /* hold low to refuse a detach */

void setup() {
  pinMode(BUSY_PIN, INPUT_PULLUP);
  dfu.onDetach([]() { return digitalRead(BUSY_PIN) == HIGH; });

  CH32RVUsbDeviceConfig config;
  config.product = "CH32RV DFU runtime";
  USBDevice.begin(config);
}

void loop() {
}
