// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

#include <CH32RVUsbDevice.h>

CH32RVUsbDevice device;
CH32RVUsbDeviceHidKeyboard keyboard(device);

static const uint8_t chord[] = {
    CH32RV_USB_HID_KEY_A, CH32RV_USB_HID_KEY_S, CH32RV_USB_HID_KEY_D, CH32RV_USB_HID_KEY_F,
    CH32RV_USB_HID_KEY_G, CH32RV_USB_HID_KEY_H, CH32RV_USB_HID_KEY_J, CH32RV_USB_HID_KEY_K,
    CH32RV_USB_HID_KEY_L, CH32RV_USB_HID_KEY_SEMICOLON,
};

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  delay(1500);

  // en: Select the report format before device.begin(). / ja: device.begin()より前にreport形式を選ぶ。
  keyboard.enableNkro();
  keyboard.setLayout(CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_EN_US);

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;
  config.pid = 0x0001;
  config.manufacturer = "ch32-riscv-ug";
  config.product = "CH32RVUsbDevice NKRO Keyboard";
  config.serialNumber = "ch32rv-keyboard-nkro";

  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.printf("USB NKRO keyboard ready (nkro=%u)\n", keyboard.nkroEnabled() ? 1 : 0);
}

void loop()
{
  static uint32_t lastSendMs = 0;
  const uint32_t now = millis();
  if (now - lastSendMs < 4000)
  {
    delay(1);
    return;
  }
  lastSendMs = now;

  if (!device.ready())
  {
    return;
  }

  // en: Build the chord first, then send one report so all keys arrive together.
  // ja: キー集合を組み立ててから一つのreportで送り、同時押しを表す。
  CH32RVUsbDeviceNkroKeyboardReport report;
  for (size_t i = 0; i < sizeof(chord); i++)
  {
    report.press(chord[i]);
  }

  // en: Left Shift is a modifier usage, stored separately from the key bitmap.
  // ja: Left Shiftは修飾usageなので、キーbitmapとは別に保存する。
  report.press(0xe1);
  keyboard.sendReport(report);
  delay(200);
  // en: Release held keys as well as sending their presses. / ja: 押下だけでなく、保持していたキーの解放も送る。
  keyboard.releaseAll();

  Serial.printf("after releaseAll: A still down? %s\n",
                keyboard.heldState().isDown(CH32RV_USB_HID_KEY_A) ? "yes" : "no");

  Serial.printf("sent %u-key chord (protocol=%s)\n",
                (unsigned)sizeof(chord),
                keyboard.protocol() == 0 ? "boot" : "report");
}
