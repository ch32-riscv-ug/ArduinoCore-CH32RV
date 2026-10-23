// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

#include <CH32RVUsbDevice.h>

CH32RVUsbDevice device;
CH32RVUsbDeviceHidKeyboard keyboard(device);
// en: Both HID functions share this device and are distinguished by report ID.
// ja: 二つのHID機能は同じデバイスに属し、Report IDで区別する。
CH32RVUsbDeviceHidMouse mouse(device);

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  delay(1500);

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;
  config.pid = 0x0001;
  config.manufacturer = "ch32-riscv-ug";
  config.product = "CH32RVUsbDevice Keyboard Mouse";
  config.serialNumber = "ch32rv-keyboard-mouse";

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
  if (!mouse.move(40, 0))
  {
    Serial.printf("MOVE_FAILED error=%s\n", device.lastErrorName());
  }
  if (!mouse.click(CH32RV_USB_DEVICE_MOUSE_LEFT))
  {
    Serial.printf("CLICK_FAILED error=%s\n", device.lastErrorName());
  }
}
