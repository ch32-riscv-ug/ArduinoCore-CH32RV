// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

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
    const uint32_t once = counter;

    // en: Only copy a consistent snapshot here; print after enabling interrupts.
    // ja: ここでは整合する値をコピーするだけにし、表示は割り込みを戻してから行う。
    noInterrupts();
    const uint32_t s = seconds;
    const uint32_t t = ticks;
    interrupts();

    Serial.print("counter=");
    Serial.print(once);
    Serial.print(" seconds=");
    Serial.print(s);
    Serial.print(" ticks=");
    Serial.println(t);

    // en: These demo values are updated in loop(), not by an attached ISR. / ja: この例の値はISRではなくloop()で更新する。
    counter++;
    seconds = millis() / 1000ul;
    ticks = millis();
    delay(1000);
}
