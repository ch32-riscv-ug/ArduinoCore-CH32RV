// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

#include <CH32RVUsbDevice.h>

CH32RVUsbDevice device;
CH32RVUsbDeviceHidMouse mouse(device);

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  delay(1500);

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;
  config.pid = 0x0001;
  config.manufacturer = "ch32-riscv-ug";
  config.product = "CH32RVUsbDevice Mouse";
  config.serialNumber = "ch32rv-mouse";

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
    // en: A press persists until a corresponding release report is sent. / ja: 押下状態は対応する解放reportを送るまで続く。
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
