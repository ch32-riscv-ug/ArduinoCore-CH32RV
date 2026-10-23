// en: Measure elapsed time and schedule output without a blocking delay.
// ja: 経過時間を測り、待ち続けずに一定間隔で出力する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    while (!Serial) {
    }

    const unsigned long before = micros();
    delayMicroseconds(1000);
    const unsigned long after = micros();
    Serial.print("delayMicroseconds(1000) took ");
    // en: Unsigned subtraction also works across counter wraparound.
    // ja: 符号なしの引き算ならカウンタの桁あふれをまたげる。
    Serial.print(after - before);
    Serial.println(" us");

    Serial.print("micros() wraps every ");
    Serial.print((0xFFFFFFFFul / 1000000ul) / 60ul);
    Serial.println(" minutes");
}

void loop()
{
    // en: static retains the previous timestamp between loop() calls instead of resetting it each time.
    // ja: staticの変数はloop()が戻っても値を保ち、前回の時刻を次回に引き継ぐ。
    static unsigned long last;
    const unsigned long now = millis();
    // en: Compare elapsed time instead of absolute timestamps.
    // ja: 時刻の大小ではなく経過時間を比較する。
    if (now - last >= 1000ul) {
        // en: Remember when this output happened; until the next interval is due, loop() can do other work.
        // ja: 今回出力した時刻を覚える。次の時刻になるまではほかの処理も実行できる。
        last = now;
        Serial.print("up ");
        Serial.print(now / 1000ul);
        Serial.println(" s");
    }
}
