// en: Use a potentiometer to set PWM duty with explicit input and output scales.
// ja: 可変抵抗の読み取りを、明示した数値範囲でPWMのdutyへ変換する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: ADC1 */

static const uint8_t INPUT_PIN = A0;
static const uint8_t PWM_PIN = PA1;
static uint32_t lastSampleMs;
static uint32_t lastPrintMs;

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    pinMode(PWM_PIN, OUTPUT);
    analogReadResolution(12);
    analogWriteResolution(8);
    analogWrite(PWM_PIN, 0);
    Serial.println("Turn the potentiometer to change PWM duty");
}

void loop()
{
    const uint32_t now = millis();
    if ((uint32_t)(now - lastSampleMs) < 10) {
        return;
    }
    lastSampleMs = now;
    const int raw = analogRead(INPUT_PIN);
    // en: Use a 32-bit intermediate; a 12-bit input maps to the 8-bit output scale.
    // ja: 32 bitで中間計算し、12 bitの入力を8 bitの出力範囲へ変換する。
    const uint8_t duty = (uint32_t)raw * 255 / 4095;
    analogWrite(PWM_PIN, duty);
    // en: Sample often but print less frequently so UART output does not dominate the loop.
    // ja: 読み取りは短い間隔で行い、UART表示は頻度を落として処理を占有させない。
    if ((uint32_t)(now - lastPrintMs) >= 500) {
        lastPrintMs = now;
        Serial.print("ADC: ");
        Serial.print(raw);
        Serial.print(" PWM: ");
        Serial.println(duty);
    }
}
