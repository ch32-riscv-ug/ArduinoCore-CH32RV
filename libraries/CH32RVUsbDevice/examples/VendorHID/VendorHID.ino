// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

#include <CH32RVUsbDevice.h>

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
  uint8_t payload[63] = {};
  payload[0] = 'E';
  payload[1] = 'S';
  payload[2] = 'P';
  payload[3] = static_cast<uint8_t>(inputCounter & 0xff);
  payload[4] = static_cast<uint8_t>((inputCounter >> 8) & 0xff);
  payload[5] = static_cast<uint8_t>((millis() / 1000) & 0xff);

  // en: Pass payload only; the HID layer handles the report ID. / ja: 渡すのはpayloadだけ。Report IDはHID層が扱う。
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

  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.println("USB vendor HID ready");
}

void loop()
{
  // en: Deliver notifications here. / ja: 通知callbackはここで実行する。
  deliverEvents();
  const uint32_t now = millis();
  if (now - lastInputMs >= 1000)
  {
    lastInputMs = now;
    sendInputReport();
  }

  delay(1);
}
