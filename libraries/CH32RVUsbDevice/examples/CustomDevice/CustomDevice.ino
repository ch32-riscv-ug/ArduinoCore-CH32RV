// en: Configure a composite USB device with CDC, DFU runtime and vendor HID.
// ja: CDC・DFU runtime・ベンダーHIDを組み合わせたUSB機器を設定する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RVUsbDevice.h>

static const int RX_PIN = RX;
static const int TX_PIN = TX;

CH32RVUsbDevice usb;
CH32RVUsbDeviceCdcSerial console(usb, "Console");
CH32RVUsbDeviceDfuRuntime dfu(usb);
CH32RVUsbDeviceHidVendor data(usb);

void setup() {
  // en: This Serial is hardware UART for diagnostics, separate from the USB connection being demonstrated.
  // ja: このSerialはログ用のハードウェアUART。ここで試すUSB通信の接続とは別。
  Serial.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);

  // en: This receives host-to-device HID Output; sendInput() sends the reply in the opposite direction.
  // ja: ホストから届いたHID Outputを受け、反対向きのInputとして返信する。
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
  // en: Run registered notification handlers here; USB transfer processing runs separately.
  // ja: 登録した通知関数をここで実行する。USB転送そのものの処理とは別。
  deliverEvents();
  while (console.available()) {
    console.write(console.read());
  }
  static bool was;
  // en: Track host configuration separately from CDC DTR.
  // ja: ホストの構成状態はCDCのDTRと分けて見る。
  if (usb.mounted() != was) {
    was = usb.mounted();
    Serial.println(was ? "host configured the device" : "host gone");
  }
}
