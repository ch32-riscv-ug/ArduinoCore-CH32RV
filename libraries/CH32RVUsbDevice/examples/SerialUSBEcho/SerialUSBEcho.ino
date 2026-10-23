// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

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
  // en: Gate periodic output on host DTR, not cable presence alone. / ja: ケーブルの有無だけでなく、ホストのDTRで周期出力を判断する。
  if (SerialUSB && millis() - last >= 1000) {
    last = millis();
    SerialUSB.print("count ");
    SerialUSB.println(count++);
  }
}
