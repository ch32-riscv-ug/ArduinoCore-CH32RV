// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。
/* requires: USBFS */

#include <CH32RVUsbDevice.h>

CH32RVUsbDevice device;
CH32RVUsbDeviceMidi MIDI(device);

static constexpr int MIDI_RX_PIN = PA3;
static constexpr int MIDI_TX_PIN = PA2;

static const int8_t CIN_TO_SERIAL_SIZE[16] = {
    -1, -1, 2, 3,
    3, 1, 2, 3,
    3, 3, 3, 3,
    2, 2, 3, 1,
};

static bool readSerialMidiMessage(CH32RVUsbDeviceMidiPacket &packet)
{
  if (!Serial1.available())
  {
    return false;
  }

  const uint8_t status = Serial1.read();
  if ((status & 0x80) == 0)
  {
    return false;
  }

  uint8_t cin = 0;
  uint8_t size = 0;
  switch (status & 0xf0)
  {
  case 0x80:
    cin = CH32RV_USB_DEVICE_MIDI_CIN_NOTE_OFF;
    size = 3;
    break;
  case 0x90:
    cin = CH32RV_USB_DEVICE_MIDI_CIN_NOTE_ON;
    size = 3;
    break;
  case 0xa0:
    cin = CH32RV_USB_DEVICE_MIDI_CIN_POLY_KEYPRESS;
    size = 3;
    break;
  case 0xb0:
    cin = CH32RV_USB_DEVICE_MIDI_CIN_CONTROL_CHANGE;
    size = 3;
    break;
  case 0xc0:
    cin = CH32RV_USB_DEVICE_MIDI_CIN_PROGRAM_CHANGE;
    size = 2;
    break;
  case 0xd0:
    cin = CH32RV_USB_DEVICE_MIDI_CIN_CHANNEL_PRESSURE;
    size = 2;
    break;
  case 0xe0:
    cin = CH32RV_USB_DEVICE_MIDI_CIN_PITCH_BEND_CHANGE;
    size = 3;
    break;
  default:
    return false;
  }

  const uint32_t start = millis();
  // en: Bound the wait for the remaining bytes; this is a basic channel-message parser.
  // ja: 残りの受信待ちに期限を設ける。基本的なchannelメッセージ用の簡易パーサー。
  while (Serial1.available() < size - 1)
  {
    if (millis() - start > 10)
    {
      return false;
    }
    delay(1);
  }

  packet.header = cin;
  packet.byte1 = status;
  packet.byte2 = size > 1 ? Serial1.read() : 0;
  packet.byte3 = size > 2 ? Serial1.read() : 0;
  return true;
}

static void writeSerialMidiMessage(const CH32RVUsbDeviceMidiPacket &packet)
{
  // en: CIN determines how many MIDI bytes this USB event contains. / ja: このUSBイベントに含まれるMIDIバイト数はCINで判断する。
  const uint8_t cin = packet.header & 0x0f;
  const int8_t size = CIN_TO_SERIAL_SIZE[cin];
  if (size <= 0)
  {
    return;
  }

  Serial1.write(packet.byte1);
  if (size > 1)
  {
    Serial1.write(packet.byte2);
  }
  if (size > 2)
  {
    Serial1.write(packet.byte3);
  }
  Serial1.flush();
}

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  delay(1500);

  Serial1.begin(31250, SERIAL_8N1, MIDI_RX_PIN, MIDI_TX_PIN);

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;
  config.pid = 0x0001;
  config.manufacturer = "ch32-riscv-ug";
  config.product = "CH32RVUsbDevice MIDI Interface";
  config.serialNumber = "ch32rv-midi-interface";

  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.println("USB MIDI interface ready");
}

void loop()
{
  CH32RVUsbDeviceMidiPacket packet;

  if (readSerialMidiMessage(packet))
  {
    if (MIDI.writePacket(packet))
    {
      Serial.printf("DIN_TO_USB status=0x%02x\n", packet.byte1);
    }
  }

  while (MIDI.readPacket(packet))
  {
    writeSerialMidiMessage(packet);
    Serial.printf("USB_TO_DIN cin=0x%02x status=0x%02x\n", packet.header & 0x0f, packet.byte1);
  }

  delay(1);
}
