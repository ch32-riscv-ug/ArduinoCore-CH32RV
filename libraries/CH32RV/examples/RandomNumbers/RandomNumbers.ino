// en: Compare repeatable random sequences and generate six-sided die values.
// ja: 乱数列の再現を確認し、サイコロの1～6に相当する値を作る。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    while (!Serial) {
    }

    // en: A fixed seed makes the sequence reproducible.
    // ja: 固定seedなら同じ列を再現できる。
    randomSeed(1);
    Serial.print("seed 1: ");
    for (int i = 0; i < 5; i++) {
        Serial.print(random(100));
        Serial.print(' ');
    }
    Serial.println();

    // en: Mix input noise and timing to vary the sequence; this does not guarantee secure randomness.
    // ja: 入力の揺れと時刻を混ぜて列を変える。暗号用の乱数が保証されるわけではない。
    randomSeed((unsigned long)analogRead(A1) ^ micros());
    Serial.print("seeded from noise: ");
    for (int i = 0; i < 5; i++) {
        Serial.print(random(100));
        Serial.print(' ');
    }
    Serial.println();
}

void loop()
{
    Serial.print("d6: ");
    // en: The upper bound is excluded.
    // ja: 上限値は含まれない。
    Serial.println(random(1, 7));
    delay(1000);
}
