// en: Make single pulses of an exact width and delay with a timer's one-pulse mode, and a square wave by output compare.
// ja: タイマーのワンパルスモードで正確な幅と遅れの1発のパルスを、出力比較で矩形波を出す。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RVTimerOutput.h>

static CH32RVOnePulse shot;
static CH32RVOutputCompare square;

/* en: The first pad of a timer channel, skipping `skip` and the Serial pins.
 * ja: タイマーのチャネルの最初の端子。`skip`とSerialの端子は使わない。 */
static int channelPin(int skip)
{
    for (int p = 0; p < NUM_DIGITAL_PINS; p++) {
        CH32RVTimerPad pad;
        if (digitalPinIsValid(p) && p != skip && p != TX && p != RX &&
            ch32rv_timer_pad_find(p, CH32RV_TIMER_ROLE(CH32RV_TIMER_PAD_CH), 0, &pad)) {
            return p;
        }
    }
    return -1;
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
    const int pulsePin = channelPin(-1);
    if (pulsePin < 0 || !shot.begin(pulsePin, HIGH)) {
        Serial.println("No timer pad free for the pulse");
        return;
    }
    Serial.print("Pulses on ");
    printPin(pulsePin);
    Serial.println(": 20 us high, 50 us after each start, every 100 ms");
    // en: Output compare on a second pad (another timer when the first one's is taken): toggle 250 us
    //     into each 1000 us period - a 500 Hz square wave.
    // ja: 2つ目の端子で出力比較（1つ目のタイマーが使用中なら別のタイマー）: 1000 usの周期ごとに250 usで反転 - 500 Hzの矩形波。
    const int squarePin = channelPin(pulsePin);
    if (squarePin >= 0 && square.begin(squarePin, 1000) && square.set(CH32RVOutputCompare::TOGGLE, 250)) {
        Serial.print("500 Hz square on ");
        printPin(squarePin);
        Serial.println();
    }
}

void loop()
{
    // en: fire() starts the counter; the hardware ends the pulse and stops it, whatever the CPU does meanwhile.
    // ja: fire()でカウンタが動き出し、パルスの終わりと停止はハードがする。その間CPUが何をしていても変わらない。
    if (!shot.fire(50, 20)) {
        Serial.println("Still busy");
    }
    delay(100);
}
