// en: Share one USB HID interface between keyboard and mouse functions.
// ja: キーボードとマウスで一つのUSB HID interfaceを共有する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RVUsbDevice.h>

// en: The device groups USB settings; the objects declared below add functions for the host.
// ja: deviceがUSB全体をまとめ、下のオブジェクト宣言でホストに見せる機能を追加する。
CH32RVUsbDevice device;
CH32RVUsbDeviceHidKeyboard keyboard(device);
// en: Both HID functions share this device and are distinguished by report ID.
// ja: 二つのHID機能は同じデバイスに属し、Report IDで区別する。
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
  config.product = "CH32RVUsbDevice Keyboard Mouse";
  config.serialNumber = "ch32rv-keyboard-mouse";

  // en: Start all configured functions together; success starts the stack, while the host configures it afterward.
  // ja: 設定した機能をまとめて開始する。成功は内部の開始を示し、ホストの認識はその後に進む。
  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.println("USB keyboard + mouse ready");
}

void loop()
{
  static uint32_t lastSendMs = 0;
  const uint32_t now = millis();
  if (now - lastSendMs < 3000)
  {
    delay(1);
    return;
  }
  lastSendMs = now;

  if (!keyboard.write("K"))
  {
    Serial.printf("KEY_FAILED error=%s\n", device.lastErrorName());
  }
  // en: A positive X value asks for relative rightward movement, not a destination on the screen.
  // ja: Xの正値は右向きの相対移動を指示する。画面上の移動先座標ではない。
  if (!mouse.move(40, 0))
  {
    Serial.printf("MOVE_FAILED error=%s\n", device.lastErrorName());
  }
  if (!mouse.click(CH32RV_USB_DEVICE_MOUSE_LEFT))
  {
    Serial.printf("CLICK_FAILED error=%s\n", device.lastErrorName());
  }
}
