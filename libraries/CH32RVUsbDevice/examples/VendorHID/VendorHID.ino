// en: Exchange vendor-defined HID reports and log host output and features.
// ja: ベンダー定義のHID reportを送り、ホストのOutputとFeatureを表示する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RVUsbDevice.h>

// en: The device groups USB settings; the objects declared below add functions for the host.
// ja: deviceがUSB全体をまとめ、下のオブジェクト宣言でホストに見せる機能を追加する。
CH32RVUsbDevice device;
CH32RVUsbDeviceHidVendor vendor(device);

static uint32_t lastInputMs = 0;
static uint32_t inputCounter = 0;

static void printReport(const char *prefix, const CH32RVUsbDeviceHidReport &report)
{
  Serial.printf("%s report_id=%u type=%u len=%u data=",
                prefix,
                report.reportId,
                report.reportType,
                report.length);
  for (uint16_t i = 0; i < report.length && report.data; ++i)
  {
    Serial.printf("%02x", report.data[i]);
    if (i + 1 < report.length)
    {
      Serial.print(' ');
    }
  }
  Serial.println();
}

static void sendInputReport()
{
  // en: Start with zeroed bytes, then fill the application fields; the payload is binary rather than text.
  // ja: 配列を0で初期化してから必要なfieldを埋める。内容は文字列ではなくバイナリ。
  uint8_t payload[63] = {};
  payload[0] = 'E';
  payload[1] = 'S';
  payload[2] = 'P';
  payload[3] = static_cast<uint8_t>(inputCounter & 0xff);
  payload[4] = static_cast<uint8_t>((inputCounter >> 8) & 0xff);
  payload[5] = static_cast<uint8_t>((millis() / 1000) & 0xff);

  // en: Pass payload only; the HID layer handles the report ID.
  // ja: 渡すのはpayloadだけ。Report IDはHID層が扱う。
  if (vendor.sendInput(payload, sizeof(payload)))
  {
    Serial.printf("VENDOR_INPUT counter=%lu\n", static_cast<unsigned long>(inputCounter));
    inputCounter++;
  }
  else
  {
    Serial.printf("VENDOR_INPUT_FAILED %s\n", device.lastErrorName());
  }
}

void setup()
{
  // en: This Serial is hardware UART for diagnostics, separate from the USB connection being demonstrated.
  // ja: このSerialはログ用のハードウェアUART。ここで試すUSB通信の接続とは別。
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  delay(1500);

  vendor.onOutputReport([](const CH32RVUsbDeviceHidReport &report)
                        { printReport("VENDOR_OUTPUT", report); });

  vendor.onFeatureReport([](const CH32RVUsbDeviceHidReport &report)
                         { printReport("VENDOR_FEATURE", report); });

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;
  config.pid = 0x0001;
  config.manufacturer = "ch32-riscv-ug";
  config.product = "CH32RVUsbDevice Vendor HID";
  config.serialNumber = "ch32rv-vendor-hid";

  // en: Start all configured functions together; success starts the stack, while the host configures it afterward.
  // ja: 設定した機能をまとめて開始する。成功は内部の開始を示し、ホストの認識はその後に進む。
  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.println("USB vendor HID ready");
}

void loop()
{
  // en: Run registered notification handlers here; USB transfer processing runs separately.
  // ja: 登録した通知関数をここで実行する。USB転送そのものの処理とは別。
  deliverEvents();
  const uint32_t now = millis();
  if (now - lastInputMs >= 1000)
  {
    lastInputMs = now;
    sendInputReport();
  }

  delay(1);
}
