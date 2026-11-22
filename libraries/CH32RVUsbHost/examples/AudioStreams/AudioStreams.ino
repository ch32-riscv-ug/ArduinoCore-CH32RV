// en: List a USB audio device's PCM streams, play a 440 Hz tone on its output and print the input's peak level.
// ja: USB オーディオの機器の PCM のストリームを表示し、出力に 440 Hz の音を流し、入力のピークのレベルを表示する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* requires: USBFS */

#include <CH32RVUsbHost.h>
#include <math.h>

CH32RVUsbHost usb;
static bool started;
static uint8_t outChannels;
static float phase;
static int16_t peak;

void setup()
{
    Serial.begin(115200);
    // en: Received PCM arrives here from deliverEvents(); keep only the loudest sample.
    // ja: 受けた PCM は deliverEvents() の中でここに来る。いちばん大きいサンプルだけ覚える。
    usb.onAudioData([](const CH32RVUsbHostAudioData &d) {
        for (size_t i = 0; i + 1 < d.length; i += 2) {
            const int16_t v = (int16_t)(d.data[i] | d.data[i + 1] << 8);
            if (abs(v) > peak) peak = (int16_t)abs(v);
        }
    });
    if (!usb.begin()) {
        Serial.print("usb.begin() failed: ");
        Serial.println(usb.lastErrorName());
    }
}

static void startStreams()
{
    CH32RVUsbHostAudioStreamInfo list[6];
    const size_t n = usb.getAudioStreams(CH32RV_USB_HOST_ANY_ADDRESS, list, 6);
    if (!n) return;
    for (size_t i = 0; i < n && i < 6; i++) {
        const CH32RVUsbHostAudioStreamInfo &s = list[i];
        Serial.printf("%s itf %u alt %u: %u ch, %u bit, %lu Hz (%u rates), UAC%u\n", s.input ? "input " : "output",
                      s.interfaceNumber, s.alternate, s.channels, s.bitsPerSample, (unsigned long)s.sampleRate,
                      s.sampleRateCount, s.protocol == CH32RV_USB_HOST_AUDIO_PROTOCOL_UAC2 ? 2 : 1);
    }
    // en: The first 16-bit stream each way, at 48 kHz when it has it.
    // ja: 向きごとに最初の 16 ビットのストリーム。48 kHz があればそれで。
    for (size_t i = 0; i < n && i < 6; i++) {
        const CH32RVUsbHostAudioStreamInfo &s = list[i];
        if (s.bitsPerSample != 16) continue;
        if (s.input && !usb.audioInputRunning()) usb.audioInputStart(s, 48000) || usb.audioInputStart(s);
        if (s.output && !usb.audioOutputRunning() && (usb.audioOutputStart(s, 48000) || usb.audioOutputStart(s))) {
            outChannels = s.channels;
        }
    }
    started = true;
}

void loop()
{
    deliverEvents();
    if (!started) {
        startStreams();
        delay(200);
        return;
    }
    // en: Keep the output queue topped up with 1 ms blocks of the tone.
    // ja: 出力のキューに 1 ms ずつの音を足し続ける。
    if (usb.audioOutputRunning()) {
        int16_t block[48 * 2];
        const size_t bytes = 48u * outChannels * 2u;
        while (outChannels && usb.audioOutputAvailableForWrite() >= bytes) {
            for (size_t f = 0; f < 48; f++) {
                const int16_t v = (int16_t)(sinf(phase) * 6000.0f);
                phase += 2.0f * (float)M_PI * 440.0f / 48000.0f;
                if (phase > 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
                for (uint8_t c = 0; c < outChannels; c++) block[f * outChannels + c] = v;
            }
            usb.audioSend((const uint8_t *)block, bytes);
        }
    }
    static uint32_t last;
    if (millis() - last >= 1000) {
        last = millis();
        Serial.printf("input peak %d, output underruns %lu\n", peak, (unsigned long)usb.audioOutputUnderruns());
        peak = 0;
    }
    if (!usb.audioInputRunning() && !usb.audioOutputRunning()) started = false;   // en: unplugged / ja: 抜かれた
}
