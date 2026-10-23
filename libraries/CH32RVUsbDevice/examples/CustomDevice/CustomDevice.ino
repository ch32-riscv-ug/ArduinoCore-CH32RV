// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

#include <CH32RVUsbDevice.h>

static const int RX_PIN = RX;
static const int TX_PIN = TX;

CH32RVUsbDevice usb;
CH32RVUsbDeviceCdcSerial console(usb, "Console");
CH32RVUsbDeviceDfuRuntime dfu(usb);
CH32RVUsbDeviceHidVendor data(usb);

void setup() {
  Serial.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);

  data.onOutputReport([](const CH32RVUsbDeviceHidReport &report) {
    // en: Echo while the callback buffer is valid; do not retain its pointer.
    // ja: 受信bufferが有効なcallback内で返信し、ポインタを持ち越さない。
    data.sendInput(report.data, report.length);
  });

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;
  config.pid = 0x0001;
  config.manufacturer = "Example Labs";
  config.product = "CH32RV custom device";
  config.serialNumber = nullptr;
  config.maxPowerMilliamps = 100;

  if (!usb.begin(config)) {
    Serial.print("USB begin failed: ");
    Serial.println(usb.lastErrorName());
    return;
  }
  Serial.println("USB started");
}

void loop() {
  // en: Deliver notifications here. / ja: 通知callbackはここで実行する。
  deliverEvents();
  while (console.available()) {
    console.write(console.read());
  }
  static bool was;
  // en: Track host configuration separately from CDC DTR. / ja: ホストの構成状態はCDCのDTRと分けて見る。
  if (usb.mounted() != was) {
    was = usb.mounted();
    Serial.println(was ? "host configured the device" : "host gone");
  }
}
