/* USBVendor - ported from EspUsbDevice's example of the same name (renamed
 * EspUsbDevice -> CH32RVUsbDevice, ESP_USB_ -> CH32RV_USB_). Runs on CH32X035/X033.
 * docs/usb.ja.md describes the library. */
#include <CH32RVUsbDevice.h>

CH32RVUsbDevice device;
CH32RVUsbDeviceVendor UsbVendor(device);

static uint32_t lastStatusMs = 0;
static uint32_t rxCount = 0;
static uint32_t controlCount = 0;

static void printHex(const uint8_t *data, size_t length)
{
  for (size_t i = 0; i < length; i++)
  {
    if (i)
    {
      Serial.print(' ');
    }
    Serial.printf("%02x", data[i]);
  }
}

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);   /* the log: RX, TX are the pads you wired */
  delay(1500);

  UsbVendor.onRx([](size_t available)
                 {
                   uint8_t buffer[64];
                   while (available > 0)
                   {
                     const size_t chunk = UsbVendor.read(buffer, min(available, sizeof(buffer)));
                     if (chunk == 0)
                     {
                       break;
                     }
                     rxCount += chunk;
                     Serial.print("VENDOR_RX ");
                     Serial.print(chunk);
                     Serial.print(" ");
                     printHex(buffer, chunk);
                     Serial.println();

                     UsbVendor.write(reinterpret_cast<const uint8_t *>("echo: "), 6);
                     UsbVendor.write(buffer, chunk);
                     UsbVendor.write(reinterpret_cast<const uint8_t *>("\r\n"), 2);
                     UsbVendor.flush();
                     available = UsbVendor.available();
                   }
                 });

  UsbVendor.onControlRequest([](const CH32RVUsbDeviceVendorControlRequest &request)
                             {
                               controlCount++;
                               Serial.printf("VENDOR_CONTROL stage=%u type=0x%02x request=0x%02x value=%u index=%u length=%u\n",
                                             request.stage,
                                             request.bmRequestType,
                                             request.bRequest,
                                             request.wValue,
                                             request.wIndex,
                                             request.wLength);

                               static const char info[] = "CH32RVUsbDeviceVendor";
                               if ((request.bmRequestType & 0x80) && request.bRequest == 0x01)
                               {
                                 return UsbVendor.sendControlResponse(request, info, min(static_cast<size_t>(request.wLength), sizeof(info) - 1));
                               }
                               if (!(request.bmRequestType & 0x80) && request.bRequest == 0x02)
                               {
                                 return UsbVendor.sendControlResponse(request);
                               }
                               return false;
                             });

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;   /* pid.codes test ID: a product needs its own */
  config.pid = 0x0001;
  config.manufacturer = "ch32-riscv-ug";
  config.product = "CH32RVUsbDevice USBVendor";
  config.serialNumber = "ch32rv-vendor";
  config.webusbEnabled = true;
  config.webusbUrl = "example.com/ch32rvusbdevice";

  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.println("USB vendor device ready");
}

void loop()
{
  const uint32_t now = millis();
  if (UsbVendor.mounted() && now - lastStatusMs >= 3000)
  {
    lastStatusMs = now;
    UsbVendor.printf("status rx=%lu control=%lu\r\n",
                     static_cast<unsigned long>(rxCount),
                     static_cast<unsigned long>(controlCount));
    UsbVendor.flush();
  }
  delay(1);
}
