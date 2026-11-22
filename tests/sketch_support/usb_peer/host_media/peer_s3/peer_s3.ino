/* The ESP32-S3 (or the ESP32-P4 on its OTG HS port) as a USB audio device for tests/peer/usb/host/media: a UAC1
 * headset (EspUsbAudioFunction), 48000 Hz, mono, 16-bit, both ways. The DUT (CH32RVUsbHost) is the host.
 *
 * Capture (to the host) carries a counter, one 16-bit sample after another, so the host can tell a lost packet.
 * Playback (from the host) is checked for the host's counter the same way.
 *
 * Commands, one per line on Serial; answers start "S3D ":
 *   ?                    S3D READY <mounted 0/1>
 *   audstat              S3D AUDRX <playback bytes> <breaks in the host's counter> <capture underruns>
 *   audreset             S3D AUDRESET
 *   audctl               S3D AUDCTL <muted 0/1> <volume, 1/256 dB> <mute / volume changes the host made> */
#include <Arduino.h>
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbAudioFunction audio(device);
EspUsbAudioPlaybackStream &playback = audio.addPlaybackStream();
EspUsbAudioCaptureStream &capture = audio.addCaptureStream();

static int16_t captureNext;
static uint32_t rxBytes, rxBreaks;
static int16_t rxExpect;
static bool rxSynced;
static bool muted;
static int16_t volume;
static uint32_t controlChanges;
static char line[120];
static size_t used;

static void run(char *cmd)
{
  char *arg = strchr(cmd, ' ');
  if (arg) *arg++ = '\0';
  if (!strcmp(cmd, "?")) Serial.printf("S3D READY %u\n", device.ready() ? 1 : 0);
  else if (!strcmp(cmd, "audstat"))
    Serial.printf("S3D AUDRX %lu %lu %lu\n", (unsigned long)rxBytes, (unsigned long)rxBreaks,
                  (unsigned long)capture.stats().underrunCount);
  else if (!strcmp(cmd, "audctl"))
    Serial.printf("S3D AUDCTL %u %d %lu\n", muted ? 1 : 0, volume, (unsigned long)controlChanges);
  else if (!strcmp(cmd, "audreset"))
  {
    rxBytes = rxBreaks = 0;
    rxSynced = false;
    Serial.println("S3D AUDRESET");
  }
  else Serial.printf("S3D UNKNOWN %s\n", cmd);
}

void setup()
{
  Serial.begin(115200);
  if (!playback.addFormat({48000, 1, 2, 16}) || !capture.addFormat({48000, 1, 2, 16}))
    Serial.printf("S3D FORMAT_FAILED %s\n", device.lastErrorName());
  EspUsbAudioConfig ac;
  ac.eventCapacity = 8;
  ac.playbackBufferFrames = 960;
  ac.captureBufferFrames = 960;
  if (audio.configure(ac) != ESP_OK) Serial.println("S3D AUDIO_CONFIG_FAILED");
  EspUsbDeviceConfig config;
  config.product = "S3 peer media";
  config.manufacturer = "EspUsbDevice";
  config.serialNumber = "S3PEERMEDIA";
#if CONFIG_IDF_TARGET_ESP32P4
  config.controller = EspUsbController::HighSpeed;   /* the OTG HS port, wired to the DUT's USBHS */
#endif
  if (!device.begin(config))
    Serial.printf("S3D BEGIN_FAILED %s\n", device.lastErrorName());
  Serial.println("S3D BOOTED");
}

void loop()
{
  int16_t block[48];
  while (capture.availableForWrite() >= sizeof block)
  {
    for (int16_t &s : block) s = captureNext++;
    capture.write(block, sizeof block);
  }
  int16_t in[96];
  size_t n;
  while ((n = playback.read(in, sizeof in)) != 0)
  {
    rxBytes += n;
    for (size_t i = 0; i < n / 2; i++)
    {
      if (rxSynced && in[i] != rxExpect) rxBreaks++;
      rxExpect = (int16_t)(in[i] + 1);
      rxSynced = true;
    }
  }
  EspUsbAudioEvent event;
  while (audio.pollEvent(event))
  {
    if (event.type == EspUsbAudioEventType::StreamStateChanged && event.target == EspUsbAudioEventTarget::Playback &&
        event.enabled)
      rxSynced = false;            /* a new stream: the counter starts anywhere */
    if (event.type == EspUsbAudioEventType::MuteChanged)
    {
      muted = event.muted;
      controlChanges++;
    }
    if (event.type == EspUsbAudioEventType::VolumeChanged)
    {
      volume = event.volumeDb256;
      controlChanges++;
    }
  }
  while (Serial.available())
  {
    const char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n')
    {
      line[used] = '\0';
      if (used) run(line);
      used = 0;
    }
    else if (used + 1 < sizeof line)
      line[used++] = c;
  }
}
