// en: Read an analog input and print its raw value and estimated voltage.
// ja: アナログ入力の生値と、おおよその電圧を表示する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

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
    // en: Read a numeric sample, not volts; conversion to voltage is a separate calculation below.
    // ja: 戻るのは電圧ではなく整数の測定値。下の式で別途電圧へ換算する。
    const int raw = analogRead(SENSOR);

    Serial.print(raw);
    Serial.print("  ");
    // en: Match this 12-bit voltage estimate to the configured read scale.
    // ja: 12 bit前提の電圧換算なので、読み取り分解能と分母をそろえる。
    Serial.print((raw * 3.3) / 4095.0, 3);
    Serial.println(" V (assuming 12-bit)");
    // en: Slow the display so readings are easy to follow; sampling is not continuous here.
    // ja: 値を追いやすいよう表示間隔を空ける。連続取り込みを行う例ではない。
    delay(500);
}
