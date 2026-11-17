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

    // en: Seed from ch32rv_random() (arduino-esp32's esp_random()) so each run differs: the hardware generator
    //     on V30x, V4x7 and H41x; elsewhere a software mix of the chip's ID and the time, not for secrets.
    // ja: ch32rv_random()（arduino-esp32のesp_random()）でseedを取り、実行ごとに列を変える。V30x・V4x7・H41xは
    //     ハードウェアの乱数、ほかは石のIDと時刻を混ぜたソフトの値で、秘密には使えない。
    randomSeed(ch32rv_random());
    Serial.print("seeded from ch32rv_random(): ");
    for (int i = 0; i < 5; i++) {
        Serial.print(random(100));
        Serial.print(' ');
    }
    Serial.println();

    // en: Random bytes straight from the source, for an ID or a key where the hardware generator exists.
    // ja: 乱数の元から直接バイト列を取る。ハードウェアの乱数がある石なら、IDや鍵に使える。
    uint8_t bytes[8];
    ch32rv_fill_random(bytes, sizeof bytes);
    Serial.print("ch32rv_fill_random(): ");
    for (unsigned i = 0; i < sizeof bytes; i++) {
        Serial.print(bytes[i] >> 4, HEX);
        Serial.print(bytes[i] & 15, HEX);
    }
#ifdef CH32RV_RNG_BASE
    Serial.println(" (hardware generator)");
#else
    Serial.println(" (software: not cryptographic)");
#endif
}

void loop()
{
    Serial.print("d6: ");
    // en: The upper bound is excluded.
    // ja: 上限値は含まれない。
    Serial.println(random(1, 7));
    delay(1000);
}
