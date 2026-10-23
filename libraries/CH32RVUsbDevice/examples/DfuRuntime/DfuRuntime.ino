// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

#include <CH32RVUsbDevice.h>

CH32RVUsbDeviceDfuRuntime dfu;

static const int BUSY_PIN = PA1;

void setup() {
  pinMode(BUSY_PIN, INPUT_PULLUP);
  // en: This immediate decision may veto the transition; do not wait inside it. / ja: その場で移行可否を返す判定。中で待たない。
  dfu.onDetach([]() { return digitalRead(BUSY_PIN) == HIGH; });

  CH32RVUsbDeviceConfig config;
  config.product = "CH32RV DFU runtime";
  USBDevice.begin(config);
}

void loop() {
}
