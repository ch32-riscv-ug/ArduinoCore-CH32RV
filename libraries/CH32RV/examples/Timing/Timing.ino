// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    while (!Serial) {
    }

    const unsigned long before = micros();
    delayMicroseconds(1000);
    const unsigned long after = micros();
    Serial.print("delayMicroseconds(1000) took ");
    // en: Unsigned subtraction also works across counter wraparound. / ja: 符号なしの引き算ならカウンタの桁あふれをまたげる。
    Serial.print(after - before);
    Serial.println(" us");

    Serial.print("micros() wraps every ");
    Serial.print((0xFFFFFFFFul / 1000000ul) / 60ul);
    Serial.println(" minutes");
}

void loop()
{
    static unsigned long last;
    const unsigned long now = millis();
    // en: Compare elapsed time instead of absolute timestamps. / ja: 時刻の大小ではなく経過時間を比較する。
    if (now - last >= 1000ul) {
        last = now;
        Serial.print("up ");
        Serial.print(now / 1000ul);
        Serial.println(" s");
    }
}
