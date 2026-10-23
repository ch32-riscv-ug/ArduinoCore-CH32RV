// en: Generate USB mouse movement, wheel and button actions.
// ja: USBマウスの移動・ホイール・ボタン操作を自動で送る。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RVUsbDevice.h>

// en: The device groups USB settings; the objects declared below add functions for the host.
// ja: deviceがUSB全体をまとめ、下のオブジェクト宣言でホストに見せる機能を追加する。
CH32RVUsbDevice device;
CH32RVUsbDeviceHidMouse mouse(device);

void setup()
{
  // en: This Serial is hardware UART for diagnostics, separate from the USB connection being demonstrated.
  // ja: このSerialはログ用のハードウェアUART。ここで試すUSB通信の接続とは別。
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  delay(1500);

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;
  config.pid = 0x0001;
  config.manufacturer = "ch32-riscv-ug";
  config.product = "CH32RVUsbDevice Mouse";
  config.serialNumber = "ch32rv-mouse";

  // en: Start all configured functions together; success starts the stack, while the host configures it afterward.
  // ja: 設定した機能をまとめて開始する。成功は内部の開始を示し、ホストの認識はその後に進む。
  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.println("USB mouse ready");
}

void loop()
{
  static uint8_t step = 0;
  static uint32_t lastSendMs = 0;
  const uint32_t now = millis();
  if (now - lastSendMs < 1000)
  {
    delay(1);
    return;
  }
  lastSendMs = now;

  bool ok = false;
  // en: Keep the step between loop() calls and wrap it through six actions instead of doing all at once.
  // ja: 巡回をまたいで段階を覚え、六つの操作を一度に行わず順番に繰り返す。
  switch (step++ % 6)
  {
  case 0:
    ok = mouse.move(30, 0);
    break;
  case 1:
    ok = mouse.move(0, 30);
    break;
  case 2:
    ok = mouse.move(-30, 0);
    break;
  case 3:
    ok = mouse.move(0, -30);
    break;
  case 4:
    ok = mouse.wheel(1);
    break;
  default:
    // en: A press persists until a corresponding release report is sent.
    // ja: 押下状態は対応する解放reportを送るまで続く。
    ok = mouse.press(CH32RV_USB_DEVICE_MOUSE_LEFT);
    delay(50);
    ok = ok && mouse.release(CH32RV_USB_DEVICE_MOUSE_LEFT);
    break;
  }

  if (!ok)
  {
    Serial.printf("MOUSE_FAILED error=%s\n", device.lastErrorName());
  }
}
