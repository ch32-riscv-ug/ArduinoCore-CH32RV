// en: Receive USB MIDI events and periodically send a test note.
// ja: USB MIDIを受信し、テスト用の音符を一定間隔で送る。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
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
  // en: Start the host controller; connected peripherals are discovered later through USB processing.
  // ja: Host controllerを開始する。接続機器は、その後のUSB処理で検出・列挙される。
  if (!usb.begin()) {
    Serial.print("usb.begin() failed: ");
    Serial.println(usb.lastErrorName());
  }
}

void loop() {
  // en: Run registered notification handlers here; USB transfer processing runs separately.
  // ja: 登録した通知関数をここで実行する。USB転送そのものの処理とは別。
  deliverEvents();
  static uint32_t last;
  // en: Send only after a MIDI function is usable; host-controller startup alone is not enough.
  // ja: MIDI機能が使える状態になってから送る。Host本体の起動成功だけでは足りない。
  if (usb.midiReady() && millis() - last >= 1000) {
    last = millis();
    usb.midiSendNoteOn(0, 60, 100);
    delay(200);
    // en: End the note as well as starting it.
    // ja: 発音だけでなく停止も送る。
    usb.midiSendNoteOff(0, 60);
  }
}
