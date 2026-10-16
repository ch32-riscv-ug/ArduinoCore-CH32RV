/* Wiring, operation and expected output: README.md / README.ja.md. */
/* KeyboardNKRO - ported from EspUsbDevice's example of the same name (renamed
 * EspUsbDevice -> CH32RVUsbDevice, ESP_USB_ -> CH32RV_USB_). Controller support and wiring: see the adjacent README files.
 * docs/usb.ja.md describes the library. */
#include <CH32RVUsbDevice.h>

// N-key rollover keyboard: any number of keys can be held at once, unlike the
// 6-key boot report. Enable it with keyboard.enableNkro() before device.begin().
// The device still answers boot protocol (BIOS) with the 6-key format, and the
// bitmap covers usages 0x00-0xDF so International/LANG (JIS) keys work too.

CH32RVUsbDevice device;
CH32RVUsbDeviceHidKeyboard keyboard(device);

// Ten simultaneous keys — impossible with the 6KRO boot report, easy with NKRO.
static const uint8_t chord[] = {
    CH32RV_USB_HID_KEY_A, CH32RV_USB_HID_KEY_S, CH32RV_USB_HID_KEY_D, CH32RV_USB_HID_KEY_F,
    CH32RV_USB_HID_KEY_G, CH32RV_USB_HID_KEY_H, CH32RV_USB_HID_KEY_J, CH32RV_USB_HID_KEY_K,
    CH32RV_USB_HID_KEY_L, CH32RV_USB_HID_KEY_SEMICOLON,
};

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);   /* the log: RX, TX are the pads you wired */
  delay(1500);

  keyboard.enableNkro();
  keyboard.setLayout(CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_EN_US);

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;   /* pid.codes test ID: a product needs its own */
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

  // Hold all ten keys down at the same time, then release them together. The
  // whole held-key state travels as ONE report, so the host sees the chord
  // appear at once. Calling pressUsage() ten times would work too, but it sends
  // ten reports and the host would observe the keys arriving one by one.
  CH32RVUsbDeviceNkroKeyboardReport report;
  for (size_t i = 0; i < sizeof(chord); i++)
  {
    report.press(chord[i]);
  }
  // Modifier usages (0xE0-0xE7) are routed into report.modifiers, not the
  // bitmap, so press() takes them like any other key. 0xE1 is Left Shift.
  report.press(0xe1);
  keyboard.sendReport(report);
  delay(200);
  keyboard.releaseAll();

  // heldState() is what the host was last told, so a state-driven sketch can
  // diff against it instead of keeping its own copy.
  Serial.printf("after releaseAll: A still down? %s\n",
                keyboard.heldState().isDown(CH32RV_USB_HID_KEY_A) ? "yes" : "no");

  Serial.printf("sent %u-key chord (protocol=%s)\n",
                (unsigned)sizeof(chord),
                keyboard.protocol() == 0 ? "boot" : "report");
}
