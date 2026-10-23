// en: Echo USB serial input and observe host baud and control-line settings.
// ja: USBシリアルの入力を返し、ホストの通信設定と制御線を観察する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RVUsbDevice.h>

// en: The device groups USB settings; the objects declared below add functions for the host.
// ja: deviceがUSB全体をまとめ、下のオブジェクト宣言でホストに見せる機能を追加する。
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
  // en: This Serial is hardware UART for diagnostics, separate from the USB connection being demonstrated.
  // ja: このSerialはログ用のハードウェアUART。ここで試すUSB通信の接続とは別。
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

  // en: This callback only logs; loop() consumes the received bytes.
  // ja: 通知では記録だけを行い、受信バイトはloop()で読む。
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

  // en: Start all configured functions together; success starts the stack, while the host configures it afterward.
  // ja: 設定した機能をまとめて開始する。成功は内部の開始を示し、ホストの認識はその後に進む。
  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.println("USB CDC serial ready");
}

void loop()
{
  // en: Run registered notification handlers here; USB transfer processing runs separately.
  // ja: 登録した通知関数をここで実行する。USB転送そのものの処理とは別。
  deliverEvents();
  while (UsbSerial.available() > 0)
  {
    // en: Use int so -1 can mean no byte; convert to a character only after checking that result.
    // ja: 未受信を示す-1を扱えるintで受け、結果を確かめてから文字に変換する。
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
  // en: An open CDC terminal is separate from successful device startup.
  // ja: CDC端末が開いている状態と、デバイスの起動成功は別。
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
