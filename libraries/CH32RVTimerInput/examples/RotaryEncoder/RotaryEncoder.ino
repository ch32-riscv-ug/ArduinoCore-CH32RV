// en: Count a quadrature rotary encoder with a timer's encoder mode, and print the position as it changes.
// ja: タイマーのエンコーダモードでロータリーエンコーダを数え、位置が変わったら表示する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RVTimerInput.h>

/* en: 1: the sketch turns a pretend encoder by driving the two pins itself (no wiring). 0: a real encoder.
 * ja: 1: 2本の端子を自分で動かして、仮のエンコーダを回す（配線不要）。0: 本物のエンコーダ。 */
#define SELF_TEST 1

static CH32RVEncoder encoder;
static int pinA = -1, pinB = -1;

/* en: Channel 1 and 2 pads of one timer, leaving the Serial pins alone.
 * ja: 1つのタイマーのチャネル1と2の端子。Serialの端子は使わない。 */
static bool pickPins()
{
    for (uint8_t t = 1; t <= 12; t++) {
        int a = -1, b = -1;
        for (int p = 0; p < NUM_DIGITAL_PINS; p++) {
            if (!digitalPinIsValid(p) || p == TX || p == RX || CH32RV_PWM_PIN_TO_TIMER(p) != t) {
                continue;
            }
            if (CH32RV_PWM_PIN_TO_CHANNEL(p) == 1 && a < 0) a = p;
            if (CH32RV_PWM_PIN_TO_CHANNEL(p) == 2 && b < 0) b = p;
        }
        if (a >= 0 && b >= 0) {
            pinA = a;
            pinB = b;
            return true;
        }
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

#if SELF_TEST
/* en: One step of the Gray sequence A,B = 00, 10, 11, 01 (A leading B: counting up).
 * ja: グレイコード A,B = 00, 10, 11, 01 を1つ進める（AがBより先: 増える方向）。 */
static void step(bool up)
{
    static const uint8_t seq[4] = {0, 1, 3, 2};
    static uint8_t phase;
    phase = (uint8_t)((phase + (up ? 1 : 3)) & 3u);
    digitalWrite(pinA, seq[phase] & 1);
    digitalWrite(pinB, (seq[phase] >> 1) & 1);
    delayMicroseconds(20);   // en: longer than the filter below / ja: 下のフィルタより長く
}
#endif

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    // en: Contacts to GND need pull-ups: the default. / ja: GNDへの接点にはプルアップが要る（既定）。
    encoder.setPull(INPUT_PULLUP);
    // en: Ignore pulses shorter than ~4 us: contact bounce. / ja: 約4 usより短いパルスは無視する（接点のチャタリング）。
    encoder.setFilter(F_CPU / 250000);
    if (!pickPins() || !encoder.attachFullQuad(pinA, pinB)) {
        Serial.println("No timer with both channel pads free");
        return;
    }
    Serial.print("Encoder A ");
    printPin(pinA);
    Serial.print(", B ");
    printPin(pinB);
    Serial.println(", 4 counts per cycle");
#if SELF_TEST
    pinMode(pinA, OUTPUT);   // en: the timer still reads the pads / ja: タイマーは端子を読み続ける
    pinMode(pinB, OUTPUT);
    digitalWrite(pinA, LOW);
    digitalWrite(pinB, LOW);
    encoder.clearCount();
#endif
}

void loop()
{
#if SELF_TEST
    // en: Every second, turn 40 steps forward and 25 back by turns: the count goes +40, -25, +40 ...
    // ja: 1秒ごとに、40ステップ進めるのと25戻すのを交互に: 数は+40、-25、+40 …と動く。
    static uint32_t last;
    static bool forward = true;
    if (millis() - last >= 1000) {
        last = millis();
        for (int i = 0; i < (forward ? 40 : 25); i++) step(forward);
        forward = !forward;
    }
#endif
    static int64_t shown = -1;
    const int64_t now = encoder.getCount();
    if (now != shown) {
        shown = now;
        Serial.print("count ");
        Serial.println((long)now);
    }
}
