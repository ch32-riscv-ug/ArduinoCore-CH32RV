// en: Send USB keyboard text and raw keys, and observe host Lock LED state.
// ja: USBキーボードで文字とキーを送り、ホストのLock LED状態を読む。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: USBFS */

#include <CH32RVUsbDevice.h>

// en: The device groups USB settings; the objects declared below add functions for the host.
// ja: deviceがUSB全体をまとめ、下のオブジェクト宣言でホストに見せる機能を追加する。
CH32RVUsbDevice device;
CH32RVUsbDeviceHidKeyboard keyboard(device);

void setup()
{
  // en: This Serial is hardware UART for diagnostics, separate from the USB connection being demonstrated.
  // ja: このSerialはログ用のハードウェアUART。ここで試すUSB通信の接続とは別。
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  delay(1500);

  keyboard.setLayout(CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_EN_US);

  // en: Lock LED notifications are delivered by deliverEvents().
  // ja: Lock LEDの通知はdeliverEvents()で受け取る。
  keyboard.onOutputReport([](const CH32RVUsbDeviceHidKeyboardOutputReport &report)
                          {
                            Serial.printf("LEDS num=%u caps=%u scroll=%u raw=0x%02x\n",
                                          report.numLock ? 1 : 0,
                                          report.capsLock ? 1 : 0,
                                          report.scrollLock ? 1 : 0,
                                          report.leds);
                          });

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;
  config.pid = 0x0001;
  config.manufacturer = "ch32-riscv-ug";
  config.product = "CH32RVUsbDevice Keyboard";
  config.serialNumber = "ch32rv-keyboard";

  // en: Start all configured functions together; success starts the stack, while the host configures it afterward.
  // ja: 設定した機能をまとめて開始する。成功は内部の開始を示し、ホストの認識はその後に進む。
  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.println("USB keyboard ready");
}

void loop()
{
  // en: Run registered notification handlers here; USB transfer processing runs separately.
  // ja: 登録した通知関数をここで実行する。USB転送そのものの処理とは別。
  deliverEvents();
  static uint32_t lastSendMs = 0;
  const uint32_t now = millis();
  if (now - lastSendMs < 3000)
  {
    delay(1);
    return;
  }
  lastSendMs = now;

  // en: Text is converted to key presses using the selected layout; it is not sent as serial text.
  // ja: 文字列を選択した配列のキー操作へ変換する。シリアル文字列として送るものではない。
  if (!keyboard.write("Hi from CH32RVUsbDevice\n"))
  {
    Serial.printf("WRITE_FAILED error=%s\n", device.lastErrorName());
  }

  keyboard.setLayout(CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_JA_JP);
  keyboard.write("@[]:\"");
  keyboard.tapKey('\n');
  keyboard.setLayout(CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_EN_US);

  // en: Raw usages bypass the selected character layout.
  // ja: raw usageは文字配列の変換を通さない。
  keyboard.tapUsage(CH32RV_USB_HID_KEY_LANG1);

  Serial.printf("last_leds=0x%02x caps=%u\n",
                keyboard.ledState().leds,
                keyboard.ledState().capsLock ? 1 : 0);
}
