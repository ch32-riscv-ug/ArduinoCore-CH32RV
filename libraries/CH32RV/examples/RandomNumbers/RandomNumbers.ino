// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    while (!Serial) {
    }

    // en: A fixed seed makes the sequence reproducible. / ja: 固定seedなら同じ列を再現できる。
    randomSeed(1);
    Serial.print("seed 1: ");
    for (int i = 0; i < 5; i++) {
        Serial.print(random(100));
        Serial.print(' ');
    }
    Serial.println();

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
    // en: The upper bound is excluded. / ja: 上限値は含まれない。
    Serial.println(random(1, 7));
    delay(1000);
}
