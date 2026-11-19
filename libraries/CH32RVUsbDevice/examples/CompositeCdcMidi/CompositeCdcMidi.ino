// en: One device with two functions, a serial port named "Console" and a MIDI port, each echoing what the PC sends.
//     Each function has its own buffers and back-pressure: a busy one does not hold up the other.
// ja: 1 台に 2 つの機能。"Console" という名前のシリアルと、MIDI の口。どちらも PC から来たものを返す。機能ごとに
//     バッファと押し戻しを持つので、片方が詰まってももう片方は止まらない。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* requires: USB_DEVICE */

#include <CH32RVUsbDevice.h>

CH32RVUsbDevice device;
CH32RVUsbDeviceCdcSerial console(device, "Console");
CH32RVUsbDeviceMidi midi(device);
static CH32RVUsbDeviceMidiPacket pending;
static bool held;

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  CH32RVUsbBulkStreamConfig serialConfig;
  serialConfig.rxCapacity = serialConfig.txCapacity = 128;
  CH32RVUsbMidiConfig midiConfig;
  midiConfig.rxPackets = midiConfig.txPackets = 16;
  CH32RVUsbDeviceConfig config;
  config.product = "CDC and MIDI echo";
  if (!console.configure(serialConfig) || !midi.configure(midiConfig) || !device.begin(config)) {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
  }
}

void loop()
{
  // en: The serial echo: only what the transmit side has room for.
  // ja: シリアルのエコー: 送る側に空きのある分だけ。
  while (console.available() && console.writeAvailable()) {
    const int value = console.read();
    if (value >= 0) {
      console.write(uint8_t(value));
    }
  }
  if (!device.ready()) {
    held = false;
    return;
  }
  if (!held) {
    held = midi.readPacket(pending);
  }
  if (held && midi.writePacket(pending)) {
    held = false;
  }
}
