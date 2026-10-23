// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。
/* requires: flash=48K */

#include <CH32RVUsbDevice.h>

CH32RVUsbDevice device;
CH32RVUsbAudioFunction audio(device);
CH32RVUsbAudioPlaybackStream &playback = audio.addPlaybackStream();

static uint32_t lastLogMs = 0;
static uint32_t receivedBytes = 0;

static void printEvents()
{
  CH32RVUsbAudioEvent event;
  while (audio.pollEvent(event))
  {
    if (event.type == CH32RVUsbAudioEventType::StreamStateChanged)
    {
      Serial.printf("AUDIO_PLAYBACK enabled=%u alt=%u\n",
                    event.enabled ? 1 : 0, event.alternateSetting);
    }
    else if (event.type == CH32RVUsbAudioEventType::MuteChanged)
    {
      Serial.printf("AUDIO_MUTE %u\n", event.muted ? 1 : 0);
    }
    else if (event.type == CH32RVUsbAudioEventType::VolumeChanged)
    {
      Serial.printf("AUDIO_VOLUME_DB256 %d\n", event.volumeDb256);
    }
  }
}

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  delay(1500);

  // en: Format fields: sample rate, channels, bytes per sample, significant bits.
  // ja: formatの順はsample rate、channel数、1 sampleのbytes、有効bit数。
  playback.addFormat({16000, 2, 2, 16});

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;
  config.pid = 0x0001;
  config.manufacturer = "ch32-riscv-ug";
  config.product = "CH32RVUsbDevice Speaker";
  config.serialNumber = "ch32rv-audio-sink";

  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.println("Audio speaker ready: UAC1, 16000 Hz, 16-bit, stereo");
}

void loop()
{
  uint8_t pcm[256];
  // en: Count actual bytes consumed; this sample does not drive an audio output.
  // ja: 実際に読めたバイト数を数える。この例では音声出力回路を駆動しない。
  receivedBytes += playback.read(pcm, sizeof(pcm));
  printEvents();

  const uint32_t now = millis();
  if (now - lastLogMs >= 1000)
  {
    lastLogMs = now;
    const CH32RVUsbAudioStreamStats stats = playback.stats();
    Serial.printf("AUDIO_RX bytes=%lu usb=%lu overruns=%lu dropped=%lu\n",
                  static_cast<unsigned long>(receivedBytes),
                  static_cast<unsigned long>(stats.transferredBytes),
                  static_cast<unsigned long>(stats.overrunCount),
                  static_cast<unsigned long>(audio.droppedEvents()));
    receivedBytes = 0;
  }
  delay(1);
}
