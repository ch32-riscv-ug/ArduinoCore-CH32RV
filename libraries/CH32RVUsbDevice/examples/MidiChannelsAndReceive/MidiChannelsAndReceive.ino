// en: Two USB MIDI ports (cables) on one device: notes received on port 1 light an LED by velocity, every message is
// en: logged with its cable and channel, and a note is echoed back on port 2 with the channel it came in on.
// ja: 1 台で USB MIDI の口（cable）を 2 つ見せる。口 1 で受けた音の強さで LED を光らせ、受けたメッセージを
// ja: cable と channel つきで表示し、音は同じ channel のまま口 2 から送り返す。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* requires: USB_DEVICE */

#include <CH32RVUsbDevice.h>

CH32RVUsbDevice device;
// en: Two cables each way: the host lists two MIDI ports in and two out.
// ja: 送受ともに cable 2 本。ホストには MIDI の口が入出力 2 つずつ見える。
CH32RVUsbDeviceMidi MIDI(device, 2, 2);

// en: An LED (through a resistor) on this pin; a PWM-capable one shows the velocity as brightness.
// ja: この端子に LED（抵抗つき）。PWM の出る端子なら強さが明るさになる。
static constexpr int LED_PIN = PA1;

static const char *kindOf(uint8_t status)
{
  switch (status & 0xf0)
  {
  case 0x80: return "note-off";
  case 0x90: return "note-on";
  case 0xa0: return "poly-pressure";
  case 0xb0: return "control";
  case 0xc0: return "program";
  case 0xd0: return "pressure";
  case 0xe0: return "pitch-bend";
  default: return "system";
  }
}

static void handle(const CH32RVUsbDeviceMidiPacket &packet)
{
  // en: The cable is the USB port (the header's high nibble); the channel is MIDI's, in the status byte's low nibble.
  // ja: cable は USB の口（header の上位 4 bit）。channel は MIDI のもので、status byte の下位 4 bit。
  const uint8_t cable = packet.header >> 4;
  const uint8_t status = packet.byte1;
  const uint8_t channel = status & 0x0f;
  Serial.printf("RX port %u channel %u %s %u %u\n", cable + 1, channel + 1, kindOf(status), packet.byte2, packet.byte3);

  const bool noteOn = (status & 0xf0) == 0x90 && packet.byte3 > 0;
  const bool noteOff = (status & 0xf0) == 0x80 || ((status & 0xf0) == 0x90 && packet.byte3 == 0);
  if (cable == 0 && (noteOn || noteOff))
  {
    // en: Velocity 1..127 to brightness; a Note On with velocity 0 is a Note Off.
    // ja: 強さ 1〜127 を明るさに。強さ 0 の Note On は Note Off。
    analogWrite(LED_PIN, noteOn ? packet.byte3 * 2 : 0);
    // en: The same note back on port 2 (cable 1), on the channel it came in on.
    // ja: 同じ音を、来た channel のまま口 2（cable 1）から返す。
    if (noteOn)
    {
      MIDI.noteOn(channel, packet.byte2, packet.byte3, 1);
    }
    else
    {
      MIDI.noteOff(channel, packet.byte2, 0, 1);
    }
  }
}

void setup()
{
  // en: Log on the hardware UART, apart from the USB connection being demonstrated.
  // ja: 表示はハードウェアの UART。ここで試す USB の接続とは別。
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  delay(1500);
  pinMode(LED_PIN, OUTPUT);

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;
  config.pid = 0x0001;
  config.manufacturer = "ch32-riscv-ug";
  config.product = "CH32RV MIDI 2 ports";
  config.serialNumber = "ch32rv-midi2";
  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.println("USB MIDI with 2 ports ready");
}

void loop()
{
  CH32RVUsbDeviceMidiPacket packet;
  while (MIDI.readPacket(packet))
  {
    handle(packet);
  }
}
