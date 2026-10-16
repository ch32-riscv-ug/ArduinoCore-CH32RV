/* Wiring, operation and expected output: README.md / README.ja.md. */
/* VendorHID - ported from EspUsbDevice's example of the same name (renamed
 * EspUsbDevice -> CH32RVUsbDevice, ESP_USB_ -> CH32RV_USB_). Controller support and wiring: see the adjacent README files.
 * docs/usb.ja.md describes the library. */
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
  Serial.begin(115200, SERIAL_8N1, RX, TX);   /* the log: RX, TX are the pads you wired */
  delay(1500);

  vendor.onOutputReport([](const CH32RVUsbDeviceHidReport &report)
                        { printReport("VENDOR_OUTPUT", report); });

  vendor.onFeatureReport([](const CH32RVUsbDeviceHidReport &report)
                         { printReport("VENDOR_FEATURE", report); });

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;   /* pid.codes test ID: a product needs its own */
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
  const uint32_t now = millis();
  if (now - lastInputMs >= 1000)
  {
    lastInputMs = now;
    sendInputReport();
  }

  delay(1);
}
