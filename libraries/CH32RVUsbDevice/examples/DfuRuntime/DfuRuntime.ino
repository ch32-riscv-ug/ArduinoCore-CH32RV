// en: Expose a USB update entry and allow a button to veto the transition.
// ja: USBの更新入口を公開し、ボタンで移行を拒否できるようにする。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RVUsbDevice.h>

CH32RVUsbDeviceDfuRuntime dfu;

static const int BUSY_PIN = PA1;

void setup() {
  // en: Released is High and permits transition; holding the pin to GND makes the decision return false.
  // ja: 開放時はHighで移行を許可し、GNDへ押している間は判定がfalseを返して移行を止める。
  pinMode(BUSY_PIN, INPUT_PULLUP);
  // en: This immediate decision may veto the transition; do not wait inside it.
  // ja: その場で移行可否を返す判定。中で待たない。
  dfu.onDetach([]() { return digitalRead(BUSY_PIN) == HIGH; });

  CH32RVUsbDeviceConfig config;
  config.product = "CH32RV DFU runtime";
  USBDevice.begin(config);
}

void loop() {
}
