/* Wiring, operation and expected output: README.md / README.ja.md. */
/* Print the messages a USB MIDI device sends, and play a note on it once a
 * second. EspUsbHost's EspUsbHostMIDI.
 *
 * requires: USBFS */
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
  static uint32_t last;
  if (usb.midiReady() && millis() - last >= 1000) {
    last = millis();
    usb.midiSendNoteOn(0, 60, 100);
    delay(200);
    usb.midiSendNoteOff(0, 60);
  }
}
