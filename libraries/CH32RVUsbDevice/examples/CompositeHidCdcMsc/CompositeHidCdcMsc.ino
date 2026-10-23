// en: Combine a USB keyboard, serial port and FAT RAM disk in one device.
// ja: USBキーボード・シリアル・FAT RAMディスクを一つにまとめる。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* requires: ram=16K */

#include <CH32RVUsbDevice.h>

// en: The device groups USB settings; the objects declared below add functions for the host.
// ja: deviceがUSB全体をまとめ、下のオブジェクト宣言でホストに見せる機能を追加する。
CH32RVUsbDevice device;
CH32RVUsbDeviceHidKeyboard keyboard(device);
CH32RVUsbDeviceCdcSerial UsbSerial(device);
CH32RVUsbDeviceMsc msc(device);

// en: Keep the backing array alive for the entire exposed-medium lifetime.
// ja: 媒体として公開している間、保存先の配列を有効に保つ。
static uint8_t storage[10 * 1024];
CH32RVUsbDeviceMscFatRamDisk disk(storage, sizeof(storage));

static char lineBuffer[128];
static size_t lineLength = 0;

static void handleCdcLine(const char *line)
{
  // en: Separate the command prefix from its text; the keyboard output targets the focused host application.
  // ja: コマンド名と続きの文字列を分ける。キー入力の宛先はホストで選択中のアプリ。
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
  // en: This Serial is hardware UART for diagnostics, separate from the USB connection being demonstrated.
  // ja: このSerialはログ用のハードウェアUART。ここで試すUSB通信の接続とは別。
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  delay(1500);

  // en: Prepare the medium before exposing all functions with device.begin().
  // ja: device.begin()で各機能を公開する前に、媒体を準備する。
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

  keyboard.setLayout(CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_EN_US);

  UsbSerial.onLineState([](const CH32RVUsbDeviceCdcLineState &state)
                        { Serial.printf("CDC_LINE_STATE dtr=%u rts=%u\n",
                                        state.dtr ? 1 : 0, state.rts ? 1 : 0); });

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;
  config.pid = 0x0001;
  config.manufacturer = "ch32-riscv-ug";
  config.product = "CH32RVUsbDevice Composite";
  config.serialNumber = "ch32rv-composite";

  // en: Start all configured functions together; success starts the stack, while the host configures it afterward.
  // ja: 設定した機能をまとめて開始する。成功は内部の開始を示し、ホストの認識はその後に進む。
  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.println("USB composite (HID keyboard + CDC serial + MSC) ready");
}

void loop()
{
  // en: Run registered notification handlers here; USB transfer processing runs separately.
  // ja: 登録した通知関数をここで実行する。USB転送そのものの処理とは別。
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
    // en: Only a completed line forms a command; USB transfer boundaries do not.
    // ja: 改行まで受信して初めてコマンドとする。USBの転送境界とは別。
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
