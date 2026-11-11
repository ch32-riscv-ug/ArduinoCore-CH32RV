// en: Capture the 16 UHSIF data lines at 200 MS/s and measure a PWM on one of them.
// ja: UHSIFのデータ線16本を200 MS/sで取り込み、そのうち1本のPWMを測る。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* board: CH32H417 */

#include <CH32RVLogicCapture.h>

CH32RVLogicCapture logic;

// en: Two halves of 16384 samples; each half lasts 82 us at 200 MS/s.
// ja: 16384サンプルの半分が2つ。200 MS/sでは半分が82 usで埋まる。
static const uint32_t SAMPLES = 32768;
static uint16_t buf[SAMPLES] __attribute__((aligned(16)));

// en: PC6 is UHSIF DATA9 and a TIM3 PWM pin, so the sketch can measure its own signal.
// ja: PC6はUHSIFのDATA9で、TIM3のPWMも出せるので、自分の信号を測れる。
static const pin_size_t PWM_PIN = PC6;
static const uint8_t DATA_BIT = 9;

void setup()
{
    Serial.begin(115200);
    analogWriteFrequency(PWM_PIN, 390625);
    analogWrite(PWM_PIN, 64);
    if (!logic.begin(UHSIF_DATA, 200000000) || !logic.startContinuous(buf, SAMPLES)) {
        // en: Wrong board, or a buffer the UHSIF cannot use (size or alignment).
        // ja: 対象外のボードか、UHSIFが使えないバッファ(大きさか位置)。
        Serial.println("UHSIF capture did not start");
        return;
    }
    Serial.printf("capturing at %lu samples/s\r\n", (unsigned long)logic.sampleRate());
}

void loop()
{
    static uint32_t last;
    if (millis() - last < 1000) {
        return;
    }
    // en: Take whichever half is ready; the halves skipped while printing count in overruns().
    // ja: 埋まっている半分を受け取る。表示している間に飛ばした半分はoverruns()に数えられる。
    const uint16_t *half = logic.readBuffer();
    if (!half) {
        return;
    }
    last = millis();
    uint32_t rises = 0, first = 0, lastRise = 0, high = 0;
    for (uint32_t i = 1; i < SAMPLES / 2; i++) {
        const bool now = (half[i] >> DATA_BIT) & 1, before = (half[i - 1] >> DATA_BIT) & 1;
        high += now;
        if (now && !before) {
            if (rises == 0) {
                first = i;
            }
            lastRise = i;
            rises++;
        }
    }
    if (rises < 2) {
        Serial.println("no signal on DATA9 (PC6)");
        return;
    }
    // en: Frequency from whole periods between the first and the last rising edge.
    // ja: 最初と最後の立ち上がりの間の周期の数から周波数を出す。
    const float samplesPerPeriod = (float)(lastRise - first) / (float)(rises - 1);
    Serial.printf("PWM %.1f Hz, duty %.1f %%, %.2f samples a period (halves %lu, skipped %lu)\r\n",
                  (double)(logic.sampleRate() / samplesPerPeriod), (double)(100.0f * high / (SAMPLES / 2 - 1)),
                  (double)samplesPerPeriod, (unsigned long)logic.halvesFilled(), (unsigned long)logic.overruns());
}
