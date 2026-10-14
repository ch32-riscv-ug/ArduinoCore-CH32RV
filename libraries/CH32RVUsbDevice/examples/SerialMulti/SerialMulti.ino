/* SerialMulti - ported from EspUsbDevice's example of the same name (renamed
 * EspUsbDevice -> CH32RVUsbDevice, ESP_USB_ -> CH32RV_USB_). Runs on CH32X035/X033.
 * docs/usb.ja.md describes the library.
 *
 * CH32: build_opt.h next to this sketch raises the number of CDC ports the USB
 * stack holds to 2 (-DCFG_TUD_CDC=2); X035's USBFS has 7 endpoints besides EP0,
 * so two ports (4) leave room for more. The ESP32-P4-only third port below
 * stays out. */
#include <CH32RVUsbDevice.h>

// Two independent USB CDC ACM serial ports on one device.
//
// The host sees two COM ports / two /dev/ttyACM* nodes, each with its own
// buffers, line coding and DTR state. A common use is separating a machine
// protocol from a human console so log output never corrupts a command stream.
//
// How many ports fit is decided by the controller, not by this sketch: each
// port costs two IN endpoints (notification + data). An S2/S3 has four
// non-control IN endpoints, so two ports is its maximum and nothing else fits
// beside them. The ESP32-P4's high-speed controller has seven, which leaves
// room for two ports plus HID and Vendor, or for three ports on their own.
// Exceeding the budget makes begin() fail with ESP_ERR_INVALID_SIZE before the
// PHY is started, so the host never sees a half-built device.

CH32RVUsbDevice device;

// The name is what the host shows for each port. Give the ports distinct
// names: they are otherwise identical ACM functions, and without names a user
// has no way to tell which node is which.
CH32RVUsbDeviceCdcSerial Console(device, "Console");
CH32RVUsbDeviceCdcSerial DataLink(device, "Data Link");

// The third port exists only where the endpoint budget has room for it. Two
// ports already spend all four of the S2/S3 controller's non-control IN
// endpoints; the P4's high-speed controller has seven, so a third fits with one
// to spare. Registering it on an S3 would not fail quietly - begin() would
// return ESP_ERR_INVALID_SIZE and the device would never enumerate - so the
// port is compiled in only where it can actually exist.
#if defined(CONFIG_IDF_TARGET_ESP32P4)
#define SERIAL_MULTI_HAS_THIRD_PORT 1
CH32RVUsbDeviceCdcSerial Telemetry(device, "Telemetry");
#else
#define SERIAL_MULTI_HAS_THIRD_PORT 0
#endif

static uint32_t lastTickMs = 0;

static void echoLine(CH32RVUsbDeviceCdcSerial &port, const char *label)
{
  while (port.available() > 0)
  {
    const int value = port.read();
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
      port.print("\r\n");
      continue;
    }
    Serial.printf("%s RX %c\n", label, c);
    port.printf("%s: %c\r\n", label, c);
  }
}

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);   /* the log: RX, TX are the pads you wired */
  delay(1500);

  Console.onLineState([](const CH32RVUsbDeviceCdcLineState &state)
                      {
                        Serial.printf("CONSOLE dtr=%u rts=%u\n",
                                      state.dtr ? 1 : 0, state.rts ? 1 : 0);
                      });

  DataLink.onLineState([](const CH32RVUsbDeviceCdcLineState &state)
                       {
                         Serial.printf("DATALINK dtr=%u rts=%u\n",
                                       state.dtr ? 1 : 0, state.rts ? 1 : 0);
                       });

#if SERIAL_MULTI_HAS_THIRD_PORT
  Telemetry.onLineState([](const CH32RVUsbDeviceCdcLineState &state)
                        {
                          Serial.printf("TELEMETRY dtr=%u rts=%u\n",
                                        state.dtr ? 1 : 0, state.rts ? 1 : 0);
                        });
#endif

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;   /* pid.codes test ID: a product needs its own */
  config.pid = 0x0001;
  // The P4 defaults to its high-speed controller, which is where the third
  // port's endpoints come from. Selecting FullSpeed here would drop the budget
  // back to the S3's four IN endpoints and make begin() refuse the third port.
  config.controller = CH32RVUsbController::Auto;
  config.manufacturer = "ch32-riscv-ug";
  config.product = "CH32RVUsbDevice DualSerial";
  // A stable serial number is what lets a host keep the same port names across
  // replugs, and lets several of these boards coexist on one machine.
  config.serialNumber = "ch32rv-dualserial-0001";

  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

#if SERIAL_MULTI_HAS_THIRD_PORT
  Serial.printf("USB CDC ready: console=port%u data=port%u telemetry=port%u capacity=%u\n",
                Console.port(), DataLink.port(), Telemetry.port(),
                CH32RVUsbDevice::maxCdcPorts());
#else
  Serial.printf("USB CDC ready: console=port%u data=port%u capacity=%u\n",
                Console.port(), DataLink.port(),
                CH32RVUsbDevice::maxCdcPorts());
#endif
}

void loop()
{
  echoLine(Console, "CONSOLE");
  echoLine(DataLink, "DATALINK");
#if SERIAL_MULTI_HAS_THIRD_PORT
  echoLine(Telemetry, "TELEMETRY");
#endif

  const uint32_t now = millis();
  if (now - lastTickMs >= 3000)
  {
    lastTickMs = now;
    if (Console.connected())
    {
      Console.printf("console tick=%lu\r\n",
                     static_cast<unsigned long>(now / 1000));
    }
    if (DataLink.connected())
    {
      DataLink.printf("data tick=%lu\r\n",
                      static_cast<unsigned long>(now / 1000));
    }
#if SERIAL_MULTI_HAS_THIRD_PORT
    if (Telemetry.connected())
    {
      Telemetry.printf("telemetry tick=%lu\r\n",
                       static_cast<unsigned long>(now / 1000));
    }
#endif
  }

  delay(1);
}
