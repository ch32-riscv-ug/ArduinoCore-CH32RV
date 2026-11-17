// en: Drive a half bridge's two sides from one timer channel: CHx and CHxN with a dead time, and a break input.
// ja: 1つのタイマーのチャネルで半ブリッジの上下を動かす: CHxとCHxN、デッドタイム、ブレーキ入力。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RVTimerOutput.h>

static const uint32_t PWM_HZ = 20000;     // en: the switching frequency / ja: スイッチングの周波数
static const uint32_t DEAD_NS = 500;      // en: both sides off around each switch / ja: 切り替えの前後で両方オフの時間

static CH32RVComplementaryPwm bridge;

/* en: A channel's pad and its complementary pad (CHx, CHxN) on one timer, channel and route, and that
 *     timer's break input on the same route if there is one - the first the chip has.
 * ja: 1つのタイマー・チャネル・経路のCHxとCHxNの端子、あれば同じ経路のブレーキ入力。チップにある最初のもの。 */
static int high = -1, low = -1, brk = -1;

static bool pickPins()
{
    for (int p = 0; p < NUM_DIGITAL_PINS; p++) {
        CH32RVTimerPad a;
        for (uint8_t i = 0; digitalPinIsValid(p) && p != TX && p != RX &&
                            ch32rv_timer_pad_find(p, CH32RV_TIMER_ROLE(CH32RV_TIMER_PAD_CH), i, &a); i++) {
            for (int q = 0; q < NUM_DIGITAL_PINS; q++) {
                CH32RVTimerPad b;
                for (uint8_t j = 0; digitalPinIsValid(q) && q != TX && q != RX &&
                                    ch32rv_timer_pad_find(q, CH32RV_TIMER_ROLE(CH32RV_TIMER_PAD_CHN), j, &b); j++) {
                    if (b.channel != a.channel || !ch32rv_timer_pads_together(&a, &b)) {
                        continue;
                    }
                    high = p;
                    low = q;
                    for (int r = 0; r < NUM_DIGITAL_PINS; r++) {
                        CH32RVTimerPad k;
                        if (digitalPinIsValid(r) && r != TX && r != RX &&
                            ch32rv_timer_pad_find(r, CH32RV_TIMER_ROLE(CH32RV_TIMER_PAD_BKIN), 0, &k) &&
                            ch32rv_timer_pads_together(&a, &k)) {
                            brk = r;
                            break;
                        }
                    }
                    return true;
                }
            }
        }
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
    // en: The dead time can be set before begin(); it is rounded up to the timer's steps.
    // ja: デッドタイムはbegin()の前でも設定できる。タイマーの刻みに切り上げる。
    bridge.setDeadTime(DEAD_NS);
    if (!pickPins() || !bridge.begin(high, low, PWM_HZ, 8)) {
        Serial.println("No CHx / CHxN pair free");
        return;
    }
    Serial.print("High side ");
    printPin(high);
    Serial.print(", low side ");
    printPin(low);
    Serial.print(", dead time ");
    Serial.print(bridge.deadTime());
    Serial.println(" ns");
    // en: The break input: pulling it to GND turns both sides off at once, in hardware.
    // ja: ブレーキ入力: GNDに落とすと、ハードが両方を即座にオフにする。
    if (brk >= 0 && bridge.attachBreak(brk, LOW, INPUT_PULLUP)) {
        Serial.print("Break input ");
        printPin(brk);
        Serial.println(" (to GND: both off)");
    }
}

void loop()
{
    // en: Sweep the duty slowly up and down.
    // ja: dutyをゆっくり上げ下げする。
    static int duty = 0, step = 4;
    duty += step;
    if (duty <= 0 || duty >= 256) {
        step = -step;
    }
    bridge.write((uint32_t)constrain(duty, 0, 256));
    if (bridge.breakActive()) {
        Serial.println("Break: both sides off");
        delay(500);
        // en: resume() succeeds only once the break input has gone back high.
        // ja: resume()は、ブレーキ入力がHighに戻ってから成功する。
        if (bridge.resume()) {
            Serial.println("Resumed");
        }
    }
    delay(20);
}
