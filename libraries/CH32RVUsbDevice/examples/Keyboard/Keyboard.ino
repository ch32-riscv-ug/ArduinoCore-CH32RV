/* Keyboard - ported from EspUsbDevice's example of the same name (renamed
 * EspUsbDevice -> CH32RVUsbDevice, ESP_USB_ -> CH32RV_USB_). Runs on CH32X035/X033.
 * docs/usb.ja.md describes the library. */
#include <CH32RVUsbDevice.h>

CH32RVUsbDevice device;
CH32RVUsbDeviceHidKeyboard keyboard(device);

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);   /* the log: RX, TX are the pads you wired */
  delay(1500);

  keyboard.setLayout(CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_EN_US);

  // The callback fires the moment the host changes the Lock LEDs. It is a single
  // slot, so keyboard.ledState() below is the way to read the same state when
  // something else (an integration layer, say) already took it.
  keyboard.onOutputReport([](const CH32RVUsbDeviceHidKeyboardOutputReport &report)
                          {
                            Serial.printf("LEDS num=%u caps=%u scroll=%u raw=0x%02x\n",
                                          report.numLock ? 1 : 0,
                                          report.capsLock ? 1 : 0,
                                          report.scrollLock ? 1 : 0,
                                          report.leds);
                          });

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;   /* pid.codes test ID: a product needs its own */
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

  // Raw HID usage remains available for keys that are not part of the ASCII wrapper.
  keyboard.tapUsage(CH32RV_USB_HID_KEY_LANG1);
  // ledState() holds the latest host output report - no need to mirror it into a
  // global from the callback.
  Serial.printf("last_leds=0x%02x caps=%u\n",
                keyboard.ledState().leds,
                keyboard.ledState().capsLock ? 1 : 0);
}
