// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。
/* requires: flash=48K */

#include <CH32RVUsbDevice.h>

CH32RVUsbDevice device;
CH32RVUsbAudioFunction audio(device);
CH32RVUsbAudioCaptureStream &capture = audio.addCaptureStream();

static constexpr uint32_t SAMPLE_RATE = 32000;
static constexpr uint32_t TONE_HZ = 440;

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

// en: The top six phase bits index one cycle of the waveform table. / ja: 位相の上位6 bitを、波形tableの一周期内の位置に使う。
static uint32_t phase = 0;
static const uint32_t phaseIncrement =
    static_cast<uint32_t>((static_cast<uint64_t>(TONE_HZ) << 32) / SAMPLE_RATE);
static uint32_t lastMicros = 0;
// en: Fractional frames in millionths; retained between loop iterations. / ja: 百万分の一frame単位の端数を、loop()間で保持する。
static uint32_t owed = 0;

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  delay(1500);

  capture.addFormat({SAMPLE_RATE, 1, 2, 16});

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;
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
    elapsed = 10000;
  }
  // en: Carry fractional sample time across rounds to avoid rounding drift.
  // ja: 端数のsample時間を次の巡回へ持ち越し、丸めによるずれを防ぐ。
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
    // en: Length is in bytes; this demo does not retain data refused by a short write.
    // ja: 長さはbytes単位。この例は部分書き込みの残りを保持しない。
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
