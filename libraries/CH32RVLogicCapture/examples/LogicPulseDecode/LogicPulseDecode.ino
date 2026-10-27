// en: Measure a digital signal's frequency and duty cycle from a continuous port capture.
// ja: ポートを連続して取り込み、デジタル信号の周波数とデューティ比を測る。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RVLogicCapture.h>

/* en: 1: the sketch drives its own test PWM on the measured pin (no wiring). 0: measure an external signal.
 * ja: 1: 測る端子に自分でテスト用のPWMを出す（配線不要）。0: 外からの信号を測る。 */
#define SELF_TEST 1
static const uint32_t TEST_HZ = 1000;   // en: the test PWM / ja: テスト用PWMの周波数
static const uint8_t TEST_DUTY = 64;    // en: 64 of 255, about 25 % / ja: 255中64、約25%
static const uint32_t RATE = 100000;    // en: samples per second; resolution 10 us / ja: 1秒あたりのサンプル数。分解能10us

#if defined(CH32RV_SERIES_CH32V002) || defined(CH32RV_SERIES_CH32V003) || defined(CH32RV_SERIES_CH32V004) || \
    defined(CH32RV_SERIES_CH32V005) || defined(CH32RV_SERIES_CH32V006) || defined(CH32RV_SERIES_CH32V007) || \
    defined(CH32RV_SERIES_CH32M007)
static const uint32_t SAMPLES = 512;    // en: 2 KB of RAM here / ja: RAMが2 KBの系列
#else
static const uint32_t SAMPLES = 2048;
#endif

static CH32RVLogicCapture logic;
static uint16_t buf[SAMPLES];
static pin_size_t pin;
static uint16_t mask;

/* en: Totals over one report: a period is rising edge to rising edge.
 * ja: 1回の表示までの集計。周期は立ち上がりから次の立ち上がりまで。 */
static uint32_t periods, periodSum, highSum, minPeriod, maxPeriod;
static uint32_t highRun, sinceRise;   // en: carried across halves / ja: 半分をまたいで引き継ぐ
static int level = -1;
static bool seenRise;

/* en: The first pad with PWM from a timer other than the capture's (as in the bench test).
 * ja: 取り込みと別のタイマーでPWMを出せる最初の端子（ベンチの試験と同じ選び方）。 */
static bool pickPin()
{
    for (pin_size_t p = 0; p < NUM_DIGITAL_PINS; p++) {
        const uint8_t t = (uint8_t)CH32RV_PWM_PIN_TO_TIMER(p);
        if (digitalPinIsCommon(p) && t && t != CH32RVLogicCapture::timer() && CH32RV_PWM_PIN_TO_CHANNEL(p)) {
            pin = p;
            return true;
        }
    }
    return false;
}

static void printTenths(uint32_t v)
{
    Serial.print(v / 10u);
    Serial.print('.');
    Serial.print(v % 10u);
}

static void scan(const uint16_t *half, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        const int v = (half[i] & mask) ? 1 : 0;
        if (v && level == 0) {                   // en: a rising edge / ja: 立ち上がり
            if (seenRise) {
                periods++;
                periodSum += sinceRise;
                highSum += highRun;
                if (sinceRise < minPeriod) minPeriod = sinceRise;
                if (sinceRise > maxPeriod) maxPeriod = sinceRise;
            }
            seenRise = true;
            sinceRise = 0;
            highRun = 0;
        }
        sinceRise++;
        highRun += (uint32_t)v;
        level = v;
    }
}

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    if (!pickPin()) {
        Serial.println("No pin with PWM on another timer");
        return;
    }
#if SELF_TEST
    analogWriteFrequency(pin, TEST_HZ);
    analogWrite(pin, TEST_DUTY);             // en: the capture reads the pad, so it sees its own output / ja: 取り込みは端子を読むので自分の出力も見える
#else
    pinMode(pin, INPUT);
#endif
    const pin_size_t pad = CH32RV_PIN_RESOLVE(pin);
    mask = (uint16_t)(1u << CH32RV_PIN_BIT(pad));
    if (!logic.begin((CH32RVLogicPort)digitalPinToPort(pin), RATE) || !logic.startContinuous(buf, SAMPLES)) {
        Serial.println("Capture did not start");
        return;
    }
    Serial.print("Measuring P");
    Serial.print((char)('A' + digitalPinToPort(pin)));
    Serial.print(CH32RV_PIN_BIT(pad));
    Serial.print(" at ");
    Serial.print(logic.sampleRate());
    Serial.println(" samples/s");
    minPeriod = 0xFFFFFFFFu;
}

void loop()
{
    if (const uint16_t *half = logic.readBuffer()) {
        scan(half, SAMPLES / 2);
    }
    static uint32_t last, lostBefore;
    if (millis() - last < 1000) {
        return;
    }
    last = millis();
    const uint32_t lost = logic.overruns() - lostBefore;
    lostBefore = logic.overruns();
    if (!periods) {
        // en: No full period: the line is held, or the signal is slower than the window.
        // ja: 1周期も無い。線が止まっているか、信号が測る幅より遅い。
        Serial.print(level < 0 ? "No samples yet" : level ? "No edges: held high" : "No edges: held low");
    } else {
        // en: Integer arithmetic in tenths: floats cost the V003 more flash than it has.
        // ja: 0.1単位の整数で計算する。浮動小数点はV003のフラッシュに入りきらない。
        printTenths((uint32_t)((uint64_t)periods * logic.sampleRate() * 10u / periodSum));
        Serial.print(" Hz, duty ");
        printTenths((uint32_t)((uint64_t)highSum * 1000u / periodSum));
        Serial.print(" %, period ");
        Serial.print(minPeriod);
        Serial.print('-');
        Serial.print(maxPeriod);
        Serial.print(" samples");
    }
    if (lost) {
        // en: A half was overwritten before it was read: the totals miss that stretch.
        // ja: 読む前に上書きされた半分がある。その区間は集計に入っていない。
        Serial.print(", lost halves ");
        Serial.print(lost);
    }
    Serial.println();
    periods = periodSum = highSum = maxPeriod = 0;
    minPeriod = 0xFFFFFFFFu;
}
