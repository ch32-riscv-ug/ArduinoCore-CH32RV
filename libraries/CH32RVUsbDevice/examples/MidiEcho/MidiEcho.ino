// en: Every MIDI packet the PC sends comes back unchanged, none lost: a packet the IN queue cannot take yet is held
//     and sent on a later turn, and the next one is read only after that.
// ja: PC から来た MIDI の packet を、そのまま、取りこぼさずに返す。IN の待ち行列がまだ受けられない packet は持っておき、
//     次の回に送ってから次を読む。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* requires: USB_DEVICE */

#include <CH32RVUsbDevice.h>

CH32RVUsbDevice device;
CH32RVUsbDeviceMidi midi(device);
static CH32RVUsbDeviceMidiPacket pending;
static bool held;

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  CH32RVUsbMidiConfig config;
  config.inCables = config.outCables = 1;
  config.rxPackets = config.txPackets = 16;     // en: packets each queue holds / ja: 待ち行列ごとの packet の数
  if (!midi.configure(config) || !device.begin()) {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
  }
}

void loop()
{
  if (!device.ready()) {
    held = false;                                // en: a reset or unplug drops what was held / ja: リセットや抜去で捨てる
    return;
  }
  if (!held) {
    held = midi.readPacket(pending);
  }
  if (held && midi.writePacket(pending)) {
    held = false;
  }
}
