// en: Send USB MIDI notes and control changes, and log received packets.
// ja: USB MIDIの音符と制御値を送り、受信packetを表示する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RVUsbDevice.h>

// en: The device groups USB settings; the objects declared below add functions for the host.
// ja: deviceがUSB全体をまとめ、下のオブジェクト宣言でホストに見せる機能を追加する。
CH32RVUsbDevice device;
CH32RVUsbDeviceMidi MIDI(device);

static uint32_t lastNoteMs = 0;
static uint8_t stepIndex = 0;
static const uint8_t NOTES[] = {60, 64, 67, 72};

static void sendStep()
{
  const uint8_t previous = NOTES[(stepIndex + 3) % 4];
  const uint8_t current = NOTES[stepIndex];

  // en: Pair note starts with endings; MIDI channel 0 is user-facing channel 1.
  // ja: 発音には停止も対で送る。APIのchannel 0は表示上のchannel 1。
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
  // en: This Serial is hardware UART for diagnostics, separate from the USB connection being demonstrated.
  // ja: このSerialはログ用のハードウェアUART。ここで試すUSB通信の接続とは別。
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  delay(1500);

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;
  config.pid = 0x0001;
  config.manufacturer = "ch32-riscv-ug";
  config.product = "CH32RVUsbDevice MIDI";
  config.serialNumber = "ch32rv-midi";

  // en: Start all configured functions together; success starts the stack, while the host configures it afterward.
  // ja: 設定した機能をまとめて開始する。成功は内部の開始を示し、ホストの認識はその後に進む。
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
  // en: Read all queued MIDI events; true means one packet was copied into packet for this iteration.
  // ja: 保留中のMIDIを順に読む。trueなら、今回のpacket変数へ一件取り出せたことを示す。
  while (MIDI.readPacket(packet))
  {
    logPacket(packet);
  }

  delay(1);
}
