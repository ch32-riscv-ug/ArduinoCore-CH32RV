// en: Measure a signal's frequency, period and pulse width with a timer's PWM-input mode - no interrupt, no sampling.
// ja: タイマーのPWM入力モードで、信号の周波数・周期・パルス幅を測る。割り込みもサンプリングも使わない。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RVTimerInput.h>

/* en: 1: the sketch plays a tone on the measured pin itself (no wiring). 0: measure an external signal.
 * ja: 1: 測る端子に自分でtone()を出す（配線不要）。0: 外からの信号を測る。 */
#define SELF_TEST 1
static const unsigned TEST_HZ = 1000;     // en: the test tone / ja: テスト用の音の周波数
/* en: The slowest signal to measure. It sets the resolution: a 16-bit timer at 48 MHz ticks
 *     every 1.5 us for 10 Hz, every 21 ns for 1 kHz.
 * ja: 測る最も遅い信号。分解能もこれで決まる: 48 MHzの16ビットのタイマーなら、10 Hzで1.5 us、1 kHzで21 nsの刻み。 */
static const uint32_t MIN_HZ = 10;

static CH32RVPulseCapture pulse;
static int pin = -1;

/* en: The first channel 1 / 2 pad of a timer other than tone()'s, leaving the Serial pins alone.
 * ja: tone()と別のタイマーのチャネル1 / 2の最初の端子。Serialの端子は使わない。 */
static bool pickPin()
{
    for (int p = 0; p < NUM_DIGITAL_PINS; p++) {
        const uint8_t t = (uint8_t)CH32RV_PWM_PIN_TO_TIMER(p);
        const uint8_t c = (uint8_t)CH32RV_PWM_PIN_TO_CHANNEL(p);
        if (!digitalPinIsValid(p) || p == TX || p == RX || !t || (c != 1 && c != 2)) {
            continue;
        }
#ifdef CH32RV_TONE_TIMER
        if (t == CH32RV_TONE_TIMER) {
            continue;
        }
#endif
        pin = p;
        return true;
    }
    return false;
}

static void printPin(int p)
{
    const pin_size_t pad = CH32RV_PIN_RESOLVE(p);
    Serial.print('P');
    Serial.print((char)('A' + digitalPinToPort(p)));
    Serial.print(CH32RV_PIN_BIT(pad));
}

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    if (!pickPin() || !pulse.begin(pin, MIN_HZ, INPUT_PULLDOWN)) {
        Serial.println("No timer pad free for the capture");
        return;
    }
    Serial.print("Measuring ");
    printPin(pin);
    Serial.print(", one tick ");
    Serial.print(1000000000u / pulse.tickHz());
    Serial.println(" ns");
#if SELF_TEST
    tone(pin, TEST_HZ);       // en: the timer reads the pad, so it sees the tone too / ja: タイマーは端子を読むので、自分の音も見える
#endif
}

void loop()
{
    static uint32_t last;
    if (millis() - last < 500) {
        return;
    }
    last = millis();
    CH32RVPulseCapture::Pulse p;
    if (pulse.read(p)) {
        // en: Integer arithmetic: floats cost the V003 more flash than it has.
        // ja: 整数で計算する。浮動小数点はV003のフラッシュに入りきらない。
        Serial.print(p.frequencyHz());
        Serial.print(" Hz, period ");
        Serial.print(p.periodMicros());
        Serial.print(" us, high ");
        Serial.print(p.highMicros());
        Serial.print(" us, duty ");
        Serial.print((uint32_t)(((uint64_t)p.high * 100u + p.period / 2u) / p.period));
        Serial.println(" %");
    } else if (pulse.stalled()) {
        // en: No rising edge for 1 / MIN_HZ: the line is held, or slower than MIN_HZ.
        // ja: 1 / MIN_HZの間、立ち上がりが無い。線が止まっているか、MIN_HZより遅い。
        Serial.println(digitalRead(pin) ? "No signal: held high" : "No signal: held low");
    } else {
        Serial.println("Waiting for a whole period");
    }
}
