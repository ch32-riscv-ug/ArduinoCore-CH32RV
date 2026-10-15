/* AudioMicrophone - ported from EspUsbDevice's example of the same name
 * (renamed EspUsbDevice -> CH32RVUsbDevice, Esp -> CH32RV). Runs on
 * CH32X035/X033. docs/usb.ja.md describes the library.
 *
 * A USB Audio Class 1 microphone that sends a 440 Hz tone. 32 kHz 16-bit
 * mono (Esp: 48 kHz): a full-speed X035 endpoint holds 64 bytes, one
 * millisecond of it. The tone comes from a table instead of sinf(), which in
 * software floating point would take longer than the samples last, and the
 * sketch writes as many samples as the time that passed calls for. */
#include <CH32RVUsbDevice.h>

CH32RVUsbDevice device;
CH32RVUsbAudioFunction audio(device);
CH32RVUsbAudioCaptureStream &capture = audio.addCaptureStream();

static constexpr uint32_t SAMPLE_RATE = 32000;
static constexpr uint32_t TONE_HZ = 440;

/* One period of a sine, amplitude 8000. */
static const int16_t SINE[64] = {
    0, 784, 1561, 2322, 3061, 3771, 4445, 5075,
    5657, 6184, 6652, 7055, 7391, 7656, 7846, 7961,
    8000, 7961, 7846, 7656, 7391, 7055, 6652, 6184,
    5657, 5075, 4445, 3771, 3061, 2322, 1561, 784,
    0, -784, -1561, -2322, -3061, -3771, -4445, -5075,
    -5657, -6184, -6652, -7055, -7391, -7656, -7846, -7961,
    -8000, -7961, -7846, -7656, -7391, -7055, -6652, -6184,
    -5657, -5075, -4445, -3771, -3061, -2322, -1561, -784,
};

/* Phase as a 32-bit fraction of a period; its top 6 bits index SINE. */
static uint32_t phase = 0;
static const uint32_t phaseIncrement =
    static_cast<uint32_t>((static_cast<uint64_t>(TONE_HZ) << 32) / SAMPLE_RATE);
static uint32_t lastMicros = 0;
static uint32_t owed = 0;   /* sample frames x 1,000,000 not written yet */

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);   /* the log: RX, TX are the pads you wired */
  delay(1500);

  capture.addFormat({SAMPLE_RATE, 1, 2, 16});

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;   /* pid.codes test ID: a product needs its own */
  config.pid = 0x0001;
  config.manufacturer = "ch32-riscv-ug";
  config.product = "CH32RVUsbDevice Microphone";
  config.serialNumber = "ch32rv-microphone";

  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.println("Audio microphone ready: UAC1, 32000 Hz, 16-bit, mono");
  lastMicros = micros();
}

void loop()
{
  const uint32_t now = micros();
  uint32_t elapsed = now - lastMicros;
  lastMicros = now;
  if (elapsed > 10000)
  {
    elapsed = 10000;   /* after a long stall, do not try to catch up */
  }
  owed += elapsed * SAMPLE_RATE;
  uint32_t frames = owed / 1000000;
  owed %= 1000000;

  int16_t samples[32];
  while (frames)
  {
    const uint32_t n = frames < 32 ? frames : 32;
    for (uint32_t i = 0; i < n; ++i)
    {
      samples[i] = SINE[phase >> 26];
      phase += phaseIncrement;
    }
    capture.write(samples, n * sizeof(samples[0]));
    frames -= n;
  }

  CH32RVUsbAudioEvent event;
  while (audio.pollEvent(event))
  {
    if (event.type == CH32RVUsbAudioEventType::StreamStateChanged)
    {
      Serial.printf("AUDIO_CAPTURE enabled=%u alt=%u\n",
                    event.enabled ? 1 : 0, event.alternateSetting);
    }
  }
  delay(1);
}
