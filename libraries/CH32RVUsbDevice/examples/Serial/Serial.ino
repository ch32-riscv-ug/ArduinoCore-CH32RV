// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

#include <CH32RVUsbDevice.h>

CH32RVUsbDevice device;
CH32RVUsbDeviceCdcSerial UsbSerial(device);

static uint32_t lastMessageMs = 0;
static uint32_t lineCodingCount = 0;
static uint32_t lineStateCount = 0;

static void printLineCoding(const CH32RVUsbDeviceCdcLineCoding &coding)
{
  Serial.printf("CDC_LINE_CODING baud=%lu stop=%u parity=%u data=%u\n",
                static_cast<unsigned long>(coding.baud),
                coding.stopBits,
                coding.parity,
                coding.dataBits);
}

static void printLineState(const CH32RVUsbDeviceCdcLineState &state)
{
  Serial.printf("CDC_LINE_STATE dtr=%u rts=%u\n",
                state.dtr ? 1 : 0,
                state.rts ? 1 : 0);
}

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  delay(1500);

  UsbSerial.onLineCoding([](const CH32RVUsbDeviceCdcLineCoding &coding)
                         {
                           lineCodingCount++;
                           printLineCoding(coding);
                         });

  UsbSerial.onLineState([](const CH32RVUsbDeviceCdcLineState &state)
                        {
                          lineStateCount++;
                          printLineState(state);
                        });

  // en: This callback only logs; loop() consumes the received bytes. / ja: 通知では記録だけを行い、受信バイトはloop()で読む。
  UsbSerial.onRx([](size_t available)
                 {
                   Serial.printf("CDC_RX available=%u\n", static_cast<unsigned>(available));
                 });

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;
  config.pid = 0x0001;
  config.manufacturer = "ch32-riscv-ug";
  config.product = "CH32RVUsbDevice Serial";
  config.serialNumber = "ch32rv-serial";

  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.println("USB CDC serial ready");
}

void loop()
{
  // en: Deliver notifications here. / ja: 通知callbackはここで実行する。
  deliverEvents();
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
      UsbSerial.print("\r\n");
      continue;
    }
    UsbSerial.print("echo: ");
    UsbSerial.write(static_cast<uint8_t>(c));
    UsbSerial.print("\r\n");
  }

  const uint32_t now = millis();
  // en: An open CDC terminal is separate from successful device startup. / ja: CDC端末が開いている状態と、デバイスの起動成功は別。
  if (UsbSerial.connected() && now - lastMessageMs >= 3000)
  {
    lastMessageMs = now;
    UsbSerial.printf("tick=%lu line_coding=%lu line_state=%lu\r\n",
                     static_cast<unsigned long>(now / 1000),
                     static_cast<unsigned long>(lineCodingCount),
                     static_cast<unsigned long>(lineStateCount));
    UsbSerial.flush();
  }

  delay(1);
}
