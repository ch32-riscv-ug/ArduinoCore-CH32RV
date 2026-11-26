// en: Demonstrate copying a group of values with interrupts briefly disabled.
// ja: 短い割り込み禁止区間で、一組の値をまとめてコピーする。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

static volatile uint32_t counter;
static volatile uint32_t seconds;
static volatile uint32_t ticks;

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    while (!Serial) {
    }
    Serial.println("noInterrupts() does not nest - one interrupts() re-enables");
}

void loop()
{
    // en: Read once into a local value; a multi-value snapshot needs the protected copy below.
    // ja: 一度読んでローカルへ保存する。複数値の一組は下の保護区間でコピーする。
    const uint32_t once = counter;

    // en: Only copy a consistent snapshot here; print after enabling interrupts.
    // ja: ここでは整合する値をコピーするだけにし、表示は割り込みを戻してから行う。
    noInterrupts();
    const uint32_t s = seconds;
    const uint32_t t = ticks;
    // en: End the short protected section before calculations or Serial output that may take time.
    // ja: 短い保護区間を終えてから、時間のかかる計算やSerial出力を行う。
    interrupts();

    Serial.print("counter=");
    Serial.print(once);
    Serial.print(" seconds=");
    Serial.print(s);
    Serial.print(" ticks=");
    Serial.println(t);

    // en: These demo values are updated in loop(), not by an attached ISR.
    // ja: この例の値はISRではなくloop()で更新する。
    counter++;
    seconds = millis() / 1000ul;
    ticks = millis();
    delay(1000);
}
