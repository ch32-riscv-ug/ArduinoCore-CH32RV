/* MIDI - ported from EspUsbDevice's example of the same name (renamed
 * EspUsbDevice -> CH32RVUsbDevice, ESP_USB_ -> CH32RV_USB_). Runs on CH32X035/X033.
 * docs/usb.ja.md describes the library. */
#include <CH32RVUsbDevice.h>

CH32RVUsbDevice device;
CH32RVUsbDeviceMidi MIDI(device);

static uint32_t lastNoteMs = 0;
static uint8_t stepIndex = 0;
static const uint8_t NOTES[] = {60, 64, 67, 72};

static void sendStep()
{
  const uint8_t previous = NOTES[(stepIndex + 3) % 4];
  const uint8_t current = NOTES[stepIndex];

  MIDI.noteOff(0, previous, 0);
  MIDI.noteOn(0, current, 96);
  MIDI.controlChange(0, 74, 32 + (stepIndex * 24));

  Serial.printf("MIDI_TX note=%u\n", current);
  stepIndex = (stepIndex + 1) % 4;
}

static void logPacket(const CH32RVUsbDeviceMidiPacket &packet)
{
  Serial.printf("MIDI_RX header=0x%02x cin=0x%02x b1=0x%02x b2=%u b3=%u\n",
                packet.header,
                packet.header & 0x0f,
                packet.byte1,
                packet.byte2,
                packet.byte3);
}

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);   /* the log: RX, TX are the pads you wired */
  delay(1500);

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;   /* pid.codes test ID: a product needs its own */
  config.pid = 0x0001;
  config.manufacturer = "ch32-riscv-ug";
  config.product = "CH32RVUsbDevice MIDI";
  config.serialNumber = "ch32rv-midi";

  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.println("USB MIDI ready");
}

void loop()
{
  const uint32_t now = millis();
  if (now - lastNoteMs >= 1000)
  {
    lastNoteMs = now;
    sendStep();
  }

  CH32RVUsbDeviceMidiPacket packet;
  while (MIDI.readPacket(packet))
  {
    logPacket(packet);
  }

  delay(1);
}
