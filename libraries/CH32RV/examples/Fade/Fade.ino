// en: Fade an LED by increasing and decreasing PWM duty.
// ja: PWMのdutyを増減させてLEDの明るさを変える。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

static const uint8_t LED = PA1;

void setup()
{
    pinMode(LED, OUTPUT);
}

void loop()
{
    // en: Visit increasing duty values; the descending loop below reverses the fade.
    // ja: dutyを少しずつ増やす。下の逆向きのループでは少しずつ減らす。
    for (int level = 0; level <= 255; level += 5) {
        // en: Change duty on a PWM-capable pin; this is not a DAC output.
        // ja: PWM対応端子のdutyを変える。DACの電圧出力ではない。
        analogWrite(LED, level);
        // en: This pause sets the visible fade speed rather than the PWM carrier frequency.
        // ja: この待ち時間が明るさの変化速度を決める。PWM自体の周波数とは別。
        delay(20);
    }
    for (int level = 255; level >= 0; level -= 5) {
        analogWrite(LED, level);
        delay(20);
    }
}
