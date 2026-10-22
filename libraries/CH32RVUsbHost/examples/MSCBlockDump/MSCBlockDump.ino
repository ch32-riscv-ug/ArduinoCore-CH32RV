/* Wiring, operation and expected output: README.md / README.ja.md. */
/* Identify a USB flash drive or card reader and dump its first block.
 * EspUsbHost's EspUsbHostMSCBlockDump.
 *
 * requires: USBFS */
#include <CH32RVUsbHost.h>

CH32RVUsbHost usb;
static uint8_t block[512];
static bool dumped;

void setup() {
  Serial.begin(115200);
  usb.onDeviceDisconnected([](const CH32RVUsbHostDeviceInfo &) { dumped = false; });
  if (!usb.begin()) {
    Serial.print("usb.begin() failed: ");
    Serial.println(usb.lastErrorName());
  }
}

void loop() {
  // The callbacks set in setup() run here, and only here.
  deliverEvents();
  if (dumped || !usb.mscReady()) {
    return;
  }
  dumped = true;
  CH32RVUsbHostMscInquiry inquiry;
  uint32_t blocks = 0, size = 0;
  if (!usb.mscWaitReady() || !usb.mscInquiry(inquiry) || !usb.mscCapacity(blocks, size)) {
    Serial.print("not ready: ");
    Serial.println(usb.lastErrorName());
    return;
  }
  Serial.print(inquiry.vendor);
  Serial.print(' ');
  Serial.print(inquiry.product);
  Serial.print(": ");
  Serial.print(blocks);
  Serial.print(" blocks of ");
  Serial.println(size);
  if (size == sizeof block && usb.mscReadBlocks(0, block, 1)) {
    for (size_t i = 0; i < sizeof block; i += 16) {
      ch32rvUsbHostPrintHex(block + i, 16);
      Serial.println();
    }
  }
}
