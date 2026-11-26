// en: Compare ADC and PWM values at different numeric resolutions.
// ja: ADCとPWMの分解能を変え、数値の尺度の違いを確認する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

static const uint8_t PWM_PIN = PA1;

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    while (!Serial) {
    }
    pinMode(PWM_PIN, OUTPUT);
}

void loop()
{
    // en: Resolution selects the numeric scale, not additional ADC precision.
    // ja: 分解能の指定は数値の尺度であり、ADCの精度を増やすものではない。
    analogReadResolution(8);
    // en: At eight requested bits, the returned numeric range is 0 to 255.
    // ja: 8 bitを指定したので、返る数値の範囲は0～255になる。
    const int eight = analogRead(A1);
    analogReadResolution(12);
    const int twelve = analogRead(A1);

    Serial.print("A1 at 8 bits: ");
    Serial.print(eight);
    Serial.print("   at 12 bits: ");
    Serial.println(twelve);

    // en: Set the write scale separately from the read scale; their settings are independent.
    // ja: 読み取りとは別に、書き込みの数値範囲を設定する。両者の設定は独立している。
    analogWriteResolution(10);
    // en: Full duty on the selected 10-bit scale.
    // ja: 指定した10 bit尺度での最大duty。
    analogWrite(PWM_PIN, 1023);
    delay(300);
    analogWrite(PWM_PIN, 0);
    analogWriteResolution(8);
    delay(700);
}
