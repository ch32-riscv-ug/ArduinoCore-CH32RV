// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

static const uint8_t LED = PA1;

void setup()
{
    pinMode(LED, OUTPUT);
}

void loop()
{
    for (int level = 0; level <= 255; level += 5) {
        // en: Change duty on a PWM-capable pin; this is not a DAC output. / ja: PWM対応端子のdutyを変える。DACの電圧出力ではない。
        analogWrite(LED, level);
        delay(20);
    }
    for (int level = 255; level >= 0; level -= 5) {
        analogWrite(LED, level);
        delay(20);
    }
}
