// en: Read an analog input and print its raw value and its voltage in mV.
// ja: アナログ入力の生値と、mV単位の電圧を表示する。
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
    // en: The voltage in mV: the reading scaled by the supply the chip measures against its internal reference.
    //     A few percent either way, not a precision measurement.
    // ja: mV単位の電圧。内部の基準電圧で測った電源電圧で読み取りを換算する。数 %の幅があり、精密な測定ではない。
    Serial.print(analogReadMilliVolts(SENSOR));
    Serial.println(" mV");
    // en: Slow the display so readings are easy to follow; sampling is not continuous here.
    // ja: 値を追いやすいよう表示間隔を空ける。連続取り込みを行う例ではない。
    delay(500);
}
