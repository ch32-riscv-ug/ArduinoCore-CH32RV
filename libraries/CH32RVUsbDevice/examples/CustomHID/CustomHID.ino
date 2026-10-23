// en: Define a custom HID report and periodically send eight data bytes.
// ja: 独自のHID reportを定義し、8 byteのデータを定期的に送る。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RVUsbDevice.h>

// en: Keep report size/count, constructor length and payload length consistent.
// ja: Report Size・Count、constructorの長さ、payload長を一致させる。
static const uint8_t REPORT_DESCRIPTOR[] = {
    0x06, 0x00, 0xff, // en: Vendor Usage Page. / ja: ベンダー定義のUsage Page。
    0x09, 0x01,       // en: Application usage 1. / ja: アプリのusage 1。
    0xa1, 0x01,       // en: Application collection. / ja: アプリのcollection開始。
    0x15, 0x00,       // en: Logical minimum 0. / ja: 論理値の最小は0。
    0x26, 0xff, 0x00, // en: Logical maximum 255. / ja: 論理値の最大は255。
    0x75, 0x08,       // en: Eight bits per element. / ja: 1要素は8 bit。
    0x95, 0x08,       // en: Eight elements per report. / ja: 1 reportは8要素。
    0x09, 0x01,       // en: Input usage 1. / ja: Inputのusage 1。
    0x81, 0x02,       // en: Data, variable, absolute Input. / ja: データ・可変・絶対値のInput。
    0xc0,             // en: End collection. / ja: collection終了。
};

// en: The device groups USB settings; the objects declared below add functions for the host.
// ja: deviceがUSB全体をまとめ、下のオブジェクト宣言でホストに見せる機能を追加する。
CH32RVUsbDevice device;
// en: The descriptor tells the host how to interpret bytes; the last argument is the Input payload size.
// ja: 記述子はバイト列の意味をホストへ伝える。最後の引数はInputのpayload長。
CH32RVUsbDeviceHidCustom customHid(device, REPORT_DESCRIPTOR, sizeof(REPORT_DESCRIPTOR), 8);

static uint32_t lastReportMs = 0;
static uint8_t counter = 0;

static void sendCustomReport()
{
  uint8_t report[8] = {
      0x45,
      0x53,
      0x50,
      counter,
      static_cast<uint8_t>(millis() & 0xff),
      static_cast<uint8_t>((millis() >> 8) & 0xff),
      0xaa,
      0x55,
  };

  if (customHid.sendReport(report, sizeof(report)))
  {
    Serial.printf("CUSTOM_HID_TX counter=%u\n", counter);
    counter++;
  }
  else
  {
    Serial.printf("CUSTOM_HID_FAILED %s\n", device.lastErrorName());
  }
}

void setup()
{
  // en: This Serial is hardware UART for diagnostics, separate from the USB connection being demonstrated.
  // ja: このSerialはログ用のハードウェアUART。ここで試すUSB通信の接続とは別。
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  delay(1500);

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;
  config.pid = 0x0001;
  config.manufacturer = "ch32-riscv-ug";
  config.product = "CH32RVUsbDevice Custom HID";
  config.serialNumber = "ch32rv-custom-hid";

  // en: Start all configured functions together; success starts the stack, while the host configures it afterward.
  // ja: 設定した機能をまとめて開始する。成功は内部の開始を示し、ホストの認識はその後に進む。
  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.println("USB custom HID ready");
}

void loop()
{
  const uint32_t now = millis();
  if (now - lastReportMs >= 1000)
  {
    lastReportMs = now;
    sendCustomReport();
  }

  delay(1);
}
