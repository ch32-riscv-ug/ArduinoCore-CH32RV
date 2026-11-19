// en: The HID reports a keyboard or mouse example does not show: a gamepad's report when it changes, media keys
// en: (consumer control: volume, play) and system keys (power, sleep), each with the device (address, VID:PID) it
// en: came from, so two devices can be told apart.
// ja: キーボードやマウスの例が扱わない HID の報告: ゲームパッドの報告（変わったとき）、メディアのキー（コンシューマー:
// ja: 音量・再生）、システムのキー（電源・スリープ）。どれもどの機器（アドレス、VID:PID）から来たかを添え、2 台を見分ける。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* requires: USBFS */

#include <CH32RVUsbHost.h>

CH32RVUsbHost usb;

static void printFrom(uint8_t address, uint8_t interfaceNumber, uint16_t vid, uint16_t pid)
{
  Serial.printf("[dev %u if %u %04x:%04x] ", address, interfaceNumber, vid, pid);
}

void setup()
{
  Serial.begin(115200);

  // en: A gamepad's report is passed as it is (its layout depends on the pad): print the bytes when they change.
  // ja: ゲームパッドの報告はそのまま渡る（並びはパッドしだい）。変わったときにバイトを表示する。
  usb.onGamepad([](const CH32RVUsbHostGamepadEvent &e) {
    if (!e.changed) {
      return;
    }
    printFrom(e.address, e.interfaceNumber, e.vid, e.pid);
    Serial.print("gamepad");
    for (size_t i = 0; i < e.reportLength; i++) {
      Serial.printf(" %02x", e.reportData[i]);
    }
    Serial.println();
  });

  // en: Consumer control: the usage is HID's Consumer page (0xE9 volume up, 0xEA volume down, 0xCD play/pause ...).
  // ja: コンシューマー: usage は HID の Consumer のページ（0xE9 音量上げ、0xEA 音量下げ、0xCD 再生/一時停止 …）。
  usb.onConsumerControl([](const CH32RVUsbHostConsumerControlEvent &e) {
    printFrom(e.address, e.interfaceNumber, e.vid, e.pid);
    Serial.printf("consumer 0x%03x %s\n", e.usage, e.pressed ? "pressed" : "released");
  });

  // en: System control: 0x81 power down, 0x82 sleep, 0x83 wake up (HID's Generic Desktop page).
  // ja: システム: 0x81 電源断、0x82 スリープ、0x83 起床（HID の Generic Desktop のページ）。
  usb.onSystemControl([](const CH32RVUsbHostSystemControlEvent &e) {
    printFrom(e.address, e.interfaceNumber, e.vid, e.pid);
    Serial.printf("system 0x%02x %s\n", e.usage, e.pressed ? "pressed" : "released");
  });

  if (!usb.begin()) {
    Serial.print("usb.begin() failed: ");
    Serial.println(usb.lastErrorName());
  }
}

void loop()
{
  deliverEvents();
}
