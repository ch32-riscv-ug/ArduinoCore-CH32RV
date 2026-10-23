// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

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
    const int eight = analogRead(A1);
    analogReadResolution(12);
    const int twelve = analogRead(A1);

    Serial.print("A1 at 8 bits: ");
    Serial.print(eight);
    Serial.print("   at 12 bits: ");
    Serial.println(twelve);

    analogWriteResolution(10);
    // en: Full duty on the selected 10-bit scale. / ja: 指定した10 bit尺度での最大duty。
    analogWrite(PWM_PIN, 1023);
    delay(300);
    analogWrite(PWM_PIN, 0);
    analogWriteResolution(8);
    delay(700);
}
