// H417 DFU check of ch32rv_flash_erase / ch32rv_flash_write: built twice (-DIMAGE='"A"' / '"B"'); A goes in by SWD,
// B through the DFU runtime, and the UART (PB7 RX / PB6 TX on the bench LinkE) shows which one runs.
#include <CH32RVUsbDevice.h>

#ifndef IMAGE
#define IMAGE "?"
#endif

CH32RVUsbDeviceDfuRuntime dfu;

void setup() {
  Serial.begin(115200, SERIAL_8N1, PB7, PB6);
  CH32RVUsbDeviceConfig config;
  config.product = "CH32RV DFU check";
  config.controller = CH32RVUsbController::FullSpeed;  // PA11 / PA12 reach the PC on the bench; SS is not plugged
  USBDevice.begin(config);
}

void loop() {
  static uint32_t last;
  if (millis() - last >= 500) {
    last = millis();
    Serial.println("dfu image " IMAGE);
  }
}
