/* The ESP32-S3 (or the ESP32-P4 on its OTG HS port) as a USB audio and video device for tests/peer/usb/host/media:
 * a UAC1 headset (EspUsbAudioFunction), 48000 Hz, mono, 16-bit, both ways, and a UVC camera (EspUsbDeviceVideo,
 * YUY2 160x120 at 15 fps, isochronous, 512-byte payloads) whose frame N starts with bytes N, ~N, 0x5A, 0xA5 and is
 * colour bars after them. The DUT (CH32RVUsbHost) is the host.
 *
 * Capture (to the host) carries a counter, one 16-bit sample after another, so the host can tell a lost packet.
 * Playback (from the host) is checked for the host's counter the same way.
 *
 * Commands, one per line on Serial; answers start "S3D ":
 *   ?                    S3D READY <mounted 0/1>
 *   audstat              S3D AUDRX <playback bytes> <breaks in the host's counter> <capture underruns>
 *   audreset             S3D AUDRESET
 *   audctl               S3D AUDCTL <muted 0/1> <volume, 1/256 dB> <mute / volume changes the host made>
 *   vidstat              S3D VIDSTAT <frames sent> <frames not completed> <streaming 0/1> */
#include <Arduino.h>
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbAudioFunction audio(device);
EspUsbAudioPlaybackStream &playback = audio.addPlaybackStream();
EspUsbAudioCaptureStream &capture = audio.addCaptureStream();
EspUsbDeviceVideo camera(device, "S3 peer camera");

static constexpr uint16_t VW = 160, VH = 120;
static uint8_t vframe[VW * VH * 2];
static volatile uint32_t vSent, vFailed;
static uint32_t vDrawn, vDue;

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
  else if (!strcmp(cmd, "vidstat"))
    Serial.printf("S3D VIDSTAT %lu %lu %u\n", (unsigned long)vSent, (unsigned long)vFailed, camera.streaming() ? 1 : 0);
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
  EspUsbVideoConfig vc;
  vc.transport = EspUsbVideoTransport::Isochronous;
  vc.payloadBytes = 512;
  if (camera.configure(vc) != ESP_OK || !camera.setFormat(EspUsbDeviceVideoFormat::Yuy2) ||
      !camera.setFrameSize(VW, VH) || !camera.setFrameRate(15))
    Serial.println("S3D VIDEO_CONFIG_FAILED");
  camera.onFrameResult([](const EspUsbTransferResult &r)
                       { if (r.status == EspUsbTransferStatus::Success) vSent++; else vFailed++; });
  for (size_t i = 4; i < sizeof vframe; i += 4)
  {
    const uint8_t y = (uint8_t)(16 + 27 * ((i / 2 % VW) / (VW / 8)));
    vframe[i] = y;
    vframe[i + 1] = 128;
    vframe[i + 2] = y;
    vframe[i + 3] = 128;
  }
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
  if (camera.streaming() && !camera.frameInFlight() && (int32_t)(millis() - vDue) >= 0)
  {
    vDue = millis() + 1000u / 15u;
    vframe[0] = (uint8_t)vDrawn;
    vframe[1] = (uint8_t)~vDrawn;
    vframe[2] = 0x5A;
    vframe[3] = 0xA5;
    vDrawn++;
    camera.submitFrame(vframe, sizeof vframe);
  }
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
