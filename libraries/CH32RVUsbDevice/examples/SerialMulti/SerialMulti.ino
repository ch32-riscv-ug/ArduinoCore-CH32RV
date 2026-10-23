// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

#include <CH32RVUsbDevice.h>

CH32RVUsbDevice device;

// en: Each named CDC port has independent input and DTR state. / ja: 名前を付けた各CDCポートは、受信とDTRを独立に持つ。
CH32RVUsbDeviceCdcSerial Console(device, "Console");
CH32RVUsbDeviceCdcSerial DataLink(device, "Data Link");

// en: This optional third-port branch is inactive in CH32 builds. / ja: この任意の第三ポート分岐はCH32ビルドでは使わない。
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
  Serial.begin(115200, SERIAL_8N1, RX, TX);
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
  config.vid = 0x1209;
  config.pid = 0x0001;

  config.controller = CH32RVUsbController::Auto;
  config.manufacturer = "ch32-riscv-ug";
  config.product = "CH32RVUsbDevice DualSerial";

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
  // en: Deliver notifications here. / ja: 通知callbackはここで実行する。
  deliverEvents();
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
