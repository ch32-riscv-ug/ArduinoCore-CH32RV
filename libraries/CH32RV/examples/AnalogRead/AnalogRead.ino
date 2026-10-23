// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

static const uint8_t SENSOR = A1;

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    while (!Serial) {
    }
    Serial.print("reading A1, which is pad number ");
    Serial.println(SENSOR);
}

void loop()
{
    const int raw = analogRead(SENSOR);

    Serial.print(raw);
    Serial.print("  ");
    // en: Match this 12-bit voltage estimate to the configured read scale.
    // ja: 12 bit前提の電圧換算なので、読み取り分解能と分母をそろえる。
    Serial.print((raw * 3.3) / 4095.0, 3);
    Serial.println(" V (assuming 12-bit)");
    delay(500);
}
