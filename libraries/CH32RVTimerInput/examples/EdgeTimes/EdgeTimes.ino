// en: Time each edge of a signal with a timer's input capture, and print the high and low times between them.
// ja: タイマーの入力捕捉でエッジごとの時刻を取り、その間のHighとLowの時間を表示する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RVTimerInput.h>

/* en: 1: the sketch plays a tone on the measured pin itself (no wiring). 0: measure an external signal.
 * ja: 1: 測る端子に自分でtone()を出す（配線不要）。0: 外からの信号を測る。 */
#define SELF_TEST 1
static const unsigned TEST_HZ = 1000;     // en: the test tone / ja: テスト用の音の周波数

static CH32RVInputCapture edges;
static int pin = -1;

/* en: The first pad that a timer channel other than tone()'s reaches (any channel, any route),
 *     leaving the Serial pins alone.
 * ja: tone()と別のタイマーのチャネルが届く最初の端子（どのチャネル・経路でも）。Serialの端子は使わない。 */
static bool pickPin()
{
    for (int p = 0; p < NUM_DIGITAL_PINS; p++) {
        CH32RVTimerPad pad;
        if (!digitalPinIsValid(p) || p == TX || p == RX ||
            !ch32rv_timer_pad_find(p, CH32RV_TIMER_ROLE(CH32RV_TIMER_PAD_CH), 0, &pad)) {
            continue;
        }
#ifdef CH32RV_TONE_TIMER
        if (pad.timer == CH32RV_TONE_TIMER) {
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
    Serial.print('P');
    Serial.print((char)('A' + digitalPinToPort(p)));
    Serial.print(CH32RV_PIN_BIT(CH32RV_PIN_RESOLVE(p)));
}

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    // en: CHANGE: rising and falling edges, each latched by the hardware at its tick (1 us by default).
    // ja: CHANGE: 立ち上がりと立ち下がり。どちらもハードがその瞬間のカウントを押さえる（既定は1 us刻み）。
    if (!pickPin() || !edges.begin(pin, CHANGE, 1000000, INPUT_PULLDOWN)) {
        Serial.println("No timer pad free for the capture");
        return;
    }
    Serial.print("Timing ");
    printPin(pin);
    Serial.println();
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
    // en: The queue holds 8 edges; older ones are kept and newer ones counted in lost() when it is full.
    //     Empty it first so the edges read next are fresh.
    // ja: キューは8個。いっぱいだと古い方を残し、新しい方をlost()に数える。読む前に一度空にして、新しいエッジを読む。
    CH32RVInputCapture::Edge e, prev;
    while (edges.read(e)) {
    }
    delay(20);
    if (!edges.read(prev)) {
        Serial.println("No edges: the line is still");
        return;
    }
    while (edges.read(e)) {
        Serial.print(prev.rising ? "high " : "low ");
        Serial.print((unsigned long)(e.ticks - prev.ticks));
        Serial.print(" us  ");
        prev = e;
    }
    Serial.println();
}
