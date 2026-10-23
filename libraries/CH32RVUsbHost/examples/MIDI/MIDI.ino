// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。
/* requires: USBFS */

#include <CH32RVUsbHost.h>

CH32RVUsbHost usb;

void setup() {
  Serial.begin(115200);
  usb.onMidiMessage([](const CH32RVUsbHostMidiMessage &m) {
    Serial.print("cable ");
    Serial.print(m.cable);
    Serial.print(": ");
    Serial.print(m.status, HEX);
    Serial.print(' ');
    Serial.print(m.data1);
    Serial.print(' ');
    Serial.println(m.data2);
  });
  if (!usb.begin()) {
    Serial.print("usb.begin() failed: ");
    Serial.println(usb.lastErrorName());
  }
}

void loop() {
  // en: Deliver notifications here. / ja: 通知callbackはここで実行する。
  deliverEvents();
  static uint32_t last;
  if (usb.midiReady() && millis() - last >= 1000) {
    last = millis();
    usb.midiSendNoteOn(0, 60, 100);
    delay(200);
    // en: End the note as well as starting it. / ja: 発音だけでなく停止も送る。
    usb.midiSendNoteOff(0, 60);
  }
}
