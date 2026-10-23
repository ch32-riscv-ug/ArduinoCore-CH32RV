// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

#include <CH32RVUsbDevice.h>

CH32RVUsbDevice device;
CH32RVUsbDeviceHidKeyboard keyboard(device);

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  delay(1500);

  keyboard.setLayout(CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_EN_US);

  // en: Lock LED notifications are delivered by deliverEvents(). / ja: Lock LEDの通知はdeliverEvents()で受け取る。
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

  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.println("USB keyboard ready");
}

void loop()
{
  // en: Deliver notifications here. / ja: 通知callbackはここで実行する。
  deliverEvents();
  static uint32_t lastSendMs = 0;
  const uint32_t now = millis();
  if (now - lastSendMs < 3000)
  {
    delay(1);
    return;
  }
  lastSendMs = now;

  if (!keyboard.write("Hi from CH32RVUsbDevice\n"))
  {
    Serial.printf("WRITE_FAILED error=%s\n", device.lastErrorName());
  }

  keyboard.setLayout(CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_JA_JP);
  keyboard.write("@[]:\"");
  keyboard.tapKey('\n');
  keyboard.setLayout(CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_EN_US);

  // en: Raw usages bypass the selected character layout. / ja: raw usageは文字配列の変換を通さない。
  keyboard.tapUsage(CH32RV_USB_HID_KEY_LANG1);

  Serial.printf("last_leds=0x%02x caps=%u\n",
                keyboard.ledState().leds,
                keyboard.ledState().capsLock ? 1 : 0);
}
