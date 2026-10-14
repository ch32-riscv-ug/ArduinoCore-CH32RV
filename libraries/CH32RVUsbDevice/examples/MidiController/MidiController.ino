/* MidiController - a one-button MIDI instrument that also listens.
 *
 * The button plays middle C; whatever the host sends is printed on SerialUSB.
 * A device can carry several functions: here MIDI and the predefined CDC port.
 * DAWs and the browser (Web MIDI) see it as a class-compliant MIDI device.
 *
 * Wiring: a push button from BUTTON_PIN to GND.
 */
#include <CH32RVUsbDevice.h>

CH32RVUsbDeviceMidi midi;

static const int BUTTON_PIN = PA1;

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  SerialUSB.begin();                          /* starts USBDevice with MIDI + CDC */
}

void loop() {
  static bool was_up = true;
  const bool up = digitalRead(BUTTON_PIN) == HIGH;
  if (up != was_up) {
    if (up) {
      midi.noteOff(60);
    } else {
      midi.noteOn(60, 100);                   /* channel 1 */
    }
    was_up = up;
  }

  uint8_t packet[4];
  while (midi.readPacket(packet)) {
    SerialUSB.print("MIDI in:");
    for (uint8_t b : packet) {
      SerialUSB.print(' ');
      SerialUSB.print(b, HEX);
    }
    SerialUSB.println();
  }
  delay(5);
}
