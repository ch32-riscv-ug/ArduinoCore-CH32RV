/* Wiring, operation and expected output: README.md / README.ja.md. */
/* CustomHID - ported from EspUsbDevice's example of the same name (renamed
 * EspUsbDevice -> CH32RVUsbDevice, ESP_USB_ -> CH32RV_USB_). Controller support and wiring: see the adjacent README files.
 * docs/usb.ja.md describes the library. */
#include <CH32RVUsbDevice.h>

static const uint8_t REPORT_DESCRIPTOR[] = {
    0x06, 0x00, 0xff, // Usage Page (Vendor Defined 0xff00)
    0x09, 0x01,       // Usage (1)
    0xa1, 0x01,       // Collection (Application)
    0x15, 0x00,       //   Logical Minimum (0)
    0x26, 0xff, 0x00, //   Logical Maximum (255)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x08,       //   Report Count (8)
    0x09, 0x01,       //   Usage (1)
    0x81, 0x02,       //   Input (Data,Var,Abs)
    0xc0,             // End Collection
};

CH32RVUsbDevice device;
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
  Serial.begin(115200, SERIAL_8N1, RX, TX);   /* the log: RX, TX are the pads you wired */
  delay(1500);

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;   /* pid.codes test ID: a product needs its own */
  config.pid = 0x0001;
  config.manufacturer = "ch32-riscv-ug";
  config.product = "CH32RVUsbDevice Custom HID";
  config.serialNumber = "ch32rv-custom-hid";

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
