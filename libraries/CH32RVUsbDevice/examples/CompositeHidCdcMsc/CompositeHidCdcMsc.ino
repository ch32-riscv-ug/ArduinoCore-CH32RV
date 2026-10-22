/* Wiring, operation and expected output: README.md / README.ja.md. */
/* CompositeHidCdcMsc - ported from EspUsbDevice's example of the same name (renamed
 * EspUsbDevice -> CH32RVUsbDevice, ESP_USB_ -> CH32RV_USB_). Controller support and wiring: see the adjacent README files.
 * docs/usb.ja.md describes the library.
 * The disk is 10 KB of RAM (EspUsbDevice's is 48 KB; X035 has 20 KB).
 * requires: ram=16K */
#include <CH32RVUsbDevice.h>

// Composite (all-in-one) USB device: HID keyboard + CDC ACM serial + MSC FAT
// RAM disk on a single CH32RVUsbDevice. Register each function with the same
// CH32RVUsbDevice instance and call begin() once; the library assigns interface
// numbers and endpoints and builds the composite configuration descriptor.
//
// On CH32 this uses four endpoint numbers: one HID, two CDC, one MSC.
// The controller budget is checked by begin(). See README.md for the CH32
// setup; ESP32-specific IN endpoint limits do not apply here.

CH32RVUsbDevice device;
CH32RVUsbDeviceHidKeyboard keyboard(device);
CH32RVUsbDeviceCdcSerial UsbSerial(device);
CH32RVUsbDeviceMsc msc(device);

// Ten KiB of RAM, including FAT metadata, sized for the X035 memory budget.
// File contents are recreated after reset.
static uint8_t storage[10 * 1024];
CH32RVUsbDeviceMscFatRamDisk disk(storage, sizeof(storage));

static char lineBuffer[128];
static size_t lineLength = 0;

// Handle one complete line received over USB CDC. "type <text>" is typed out on
// the HID keyboard; anything else is echoed back over CDC.
static void handleCdcLine(const char *line)
{
  if (strncmp(line, "type ", 5) == 0)
  {
    if (keyboard.write(line + 5) && keyboard.tapKey('\n'))
    {
      UsbSerial.printf("typed: %s\r\n", line + 5);
    }
    else
    {
      UsbSerial.printf("type_failed error=%s\r\n", device.lastErrorName());
    }
    return;
  }
  UsbSerial.printf("echo: %s\r\n", line);
}

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);   /* the log: RX, TX are the pads you wired */
  delay(1500);

  // --- MSC: a FAT-formatted RAM disk that shows up as a removable drive. ---
  if (!disk.format("CH32RV"))
  {
    Serial.println("FAT_FORMAT_FAILED");
    return;
  }
  disk.addTextFile("README.TXT",
                   "CH32RVUsbDevice composite device\r\n"
                   "\r\n"
                   "This drive is also a USB keyboard and a USB serial port.\r\n"
                   "Open the serial port and send: type Hello\r\n");
  msc.vendorID("CH32RV");
  msc.productID("COMPOSITE");
  msc.productRevision("1.0");
  msc.mediaPresent(true);
  msc.isWritable(true);
  if (!disk.attach(msc))
  {
    Serial.println("MSC_ATTACH_FAILED");
    return;
  }

  // --- HID keyboard ---
  keyboard.setLayout(CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_EN_US);

  // --- CDC serial ---
  UsbSerial.onLineState([](const CH32RVUsbDeviceCdcLineState &state)
                        { Serial.printf("CDC_LINE_STATE dtr=%u rts=%u\n",
                                        state.dtr ? 1 : 0, state.rts ? 1 : 0); });

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;   /* pid.codes test ID: a product needs its own */
  config.pid = 0x0001;
  config.manufacturer = "ch32-riscv-ug";
  config.product = "CH32RVUsbDevice Composite";
  config.serialNumber = "ch32rv-composite";

  // A single begin() brings up all three functions as one composite device.
  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.println("USB composite (HID keyboard + CDC serial + MSC) ready");
}

void loop()
{
  // The callbacks set in setup() run here, and only here.
  deliverEvents();
  // Assemble CDC input into lines and act on each completed line.
  while (UsbSerial.available() > 0)
  {
    const int value = UsbSerial.read();
    if (value < 0)
    {
      break;
    }
    const char c = static_cast<char>(value);
    if (c == '\r')
    {
      continue;
    }
    if (c == '\n')
    {
      lineBuffer[lineLength] = '\0';
      handleCdcLine(lineBuffer);
      lineLength = 0;
      continue;
    }
    if (lineLength < sizeof(lineBuffer) - 1)
    {
      lineBuffer[lineLength++] = c;
    }
  }

  // Periodic heartbeat over CDC so a connected host sees the device is alive.
  static uint32_t lastTickMs = 0;
  const uint32_t now = millis();
  if (UsbSerial.connected() && now - lastTickMs >= 5000)
  {
    lastTickMs = now;
    UsbSerial.printf("tick=%lu (send 'type <text>' to use the keyboard)\r\n",
                     static_cast<unsigned long>(now / 1000));
    UsbSerial.flush();
  }

  delay(1);
}
