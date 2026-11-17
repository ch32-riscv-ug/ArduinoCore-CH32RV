// en: Take a new firmware over USB DFU while the sketch runs (dfu-util -a Firmware -D).
// ja: スケッチを動かしたまま、USB の DFU で新しいファームウェアを受け取る（dfu-util -a Firmware -D）。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* requires: USB_DEVICE */

#include <CH32RVUsbDevice.h>

// en: A DFU interface in DFU mode beside the default DFU runtime; it uses no endpoint.
// ja: 既定の DFU runtime の隣に、DFU mode の interface を置く。エンドポイントは使わない。
CH32RVUsbDeviceDfu dfu;

static volatile size_t received;
static volatile uint8_t failure;

void setup() {
  Serial.begin(115200);
  // en: These run inside the USB service while the host waits: keep them short.
  // ja: ホストが待っている間に USB の処理の中から呼ばれる。短くする。
  dfu.onProgress([](size_t n) { received = n; });
  dfu.onError([](CH32RVUsbDfuStatus status) { failure = (uint8_t)status; });

  CH32RVUsbDeviceConfig config;
  config.product = "CH32RV FirmwareDFU";
  USBDevice.begin(config);
  Serial.println("FirmwareDFU: dfu-util -a Firmware -D <sketch>.ino.bin");
}

void loop() {
  static size_t shown;
  static uint8_t failed;
  if (received != shown) {
    shown = received;
    Serial.print("received ");
    Serial.println((unsigned long)shown);
  }
  if (failure != failed) {
    failed = failure;
    Serial.print("DFU status ");
    Serial.println(failed);
  }
  delay(100);
}
