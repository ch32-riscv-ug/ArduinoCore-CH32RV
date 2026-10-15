/* AudioSpeaker - ported from EspUsbDevice's example of the same name (renamed
 * EspUsbDevice -> CH32RVUsbDevice, Esp -> CH32RV). Runs on CH32X035/X033.
 * docs/usb.ja.md describes the library.
 *
 * A USB Audio Class 1 speaker the host plays into; the sketch counts what
 * arrives and logs it on Serial. 16 kHz 16-bit stereo (Esp: 48 kHz): a
 * full-speed X035 endpoint holds 64 bytes, one millisecond of it. *
 * requires: flash=48K
 */
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
  Serial.begin(115200, SERIAL_8N1, RX, TX);   /* the log: RX, TX are the pads you wired */
  delay(1500);

  playback.addFormat({16000, 2, 2, 16});

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;   /* pid.codes test ID: a product needs its own */
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
