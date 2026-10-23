// en: Send generated axis, hat and button states as a USB gamepad.
// ja: 軸・ハット・ボタンのテスト値をUSBゲームパッドとして送る。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RVUsbDevice.h>

// en: The device groups USB settings; the objects declared below add functions for the host.
// ja: deviceがUSB全体をまとめ、下のオブジェクト宣言でホストに見せる機能を追加する。
CH32RVUsbDevice device;
CH32RVUsbDeviceHidGamepad gamepad(device);

static uint32_t lastStepMs = 0;
static uint8_t stepIndex = 0;

static const uint8_t HATS[] = {
    CH32RV_USB_DEVICE_GAMEPAD_HAT_CENTER,
    CH32RV_USB_DEVICE_GAMEPAD_HAT_UP,
    CH32RV_USB_DEVICE_GAMEPAD_HAT_UP_RIGHT,
    CH32RV_USB_DEVICE_GAMEPAD_HAT_RIGHT,
    CH32RV_USB_DEVICE_GAMEPAD_HAT_DOWN_RIGHT,
    CH32RV_USB_DEVICE_GAMEPAD_HAT_DOWN,
    CH32RV_USB_DEVICE_GAMEPAD_HAT_DOWN_LEFT,
    CH32RV_USB_DEVICE_GAMEPAD_HAT_LEFT,
    CH32RV_USB_DEVICE_GAMEPAD_HAT_UP_LEFT,
};

static void sendStep()
{
  static const int8_t AXIS[] = {0, 64, 127, 64, 0, -64, -127, -64};
  const uint8_t axisIndex = stepIndex % 8;
  const int8_t x = AXIS[axisIndex];
  const int8_t y = AXIS[(axisIndex + 2) % 8];
  const int8_t z = AXIS[(axisIndex + 4) % 8];
  const int8_t rz = AXIS[(axisIndex + 6) % 8];
  const uint8_t hat = HATS[stepIndex % (sizeof(HATS) / sizeof(HATS[0]))];
  // en: Send the full button bitmap, not only the changed button.
  // ja: 変化したボタンだけでなく、全ボタンのbitmapを送る。
  const uint32_t buttons = 1UL << (stepIndex % 8);

  // en: Send axes, hat and buttons as one current-state report, including neutral or released values.
  // ja: 軸・ハット・ボタンを現在の全状態として送る。中立や解放の値も含める。
  if (gamepad.send(x, y, z, rz, -x, -y, hat, buttons))
  {
    Serial.printf("GAMEPAD_TX x=%d y=%d z=%d rz=%d hat=%u buttons=0x%08lx\n",
                  x,
                  y,
                  z,
                  rz,
                  hat,
                  static_cast<unsigned long>(buttons));
  }
  else
  {
    Serial.printf("GAMEPAD_SEND_FAILED %s\n", device.lastErrorName());
  }

  stepIndex++;
}

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
  config.product = "CH32RVUsbDevice Gamepad";
  config.serialNumber = "ch32rv-gamepad";

  // en: Start all configured functions together; success starts the stack, while the host configures it afterward.
  // ja: 設定した機能をまとめて開始する。成功は内部の開始を示し、ホストの認識はその後に進む。
  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.println("USB gamepad ready");
}

void loop()
{
  const uint32_t now = millis();
  if (now - lastStepMs >= 500)
  {
    lastStepMs = now;
    sendStep();
  }

  delay(1);
}
