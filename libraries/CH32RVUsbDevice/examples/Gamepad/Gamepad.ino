// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

#include <CH32RVUsbDevice.h>

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
  // en: Send the full button bitmap, not only the changed button. / ja: 変化したボタンだけでなく、全ボタンのbitmapを送る。
  const uint32_t buttons = 1UL << (stepIndex % 8);

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
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  delay(1500);

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;
  config.pid = 0x0001;
  config.manufacturer = "ch32-riscv-ug";
  config.product = "CH32RVUsbDevice Gamepad";
  config.serialNumber = "ch32rv-gamepad";

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
