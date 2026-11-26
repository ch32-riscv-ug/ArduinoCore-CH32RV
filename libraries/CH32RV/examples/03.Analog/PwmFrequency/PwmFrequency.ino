// en: Change PWM frequency independently of its duty cycle.
// ja: PWMのdutyを保ったまま周波数を切り替える。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

static const uint8_t PWM_PIN = PA1;
static const uint32_t FREQUENCIES[] = {100, 1000, 5000};
static uint8_t frequencyIndex;

static void applyPwm()
{
    // en: Frequency is a shared setting in this core; this call is not isolated to one pin.
    // ja: このコアでは周波数設定を共有する。この呼び出しは指定ピンだけに閉じない。
    analogWriteFrequency(PWM_PIN, FREQUENCIES[frequencyIndex]);
    analogWriteResolution(8);
    // en: Rewrite the duty after changing frequency so the timer configuration is applied.
    // ja: 周波数変更後にdutyを書き直し、タイマーへ設定を反映する。
    analogWrite(PWM_PIN, 64);
    Serial.print("Requested Hz: ");
    Serial.println(FREQUENCIES[frequencyIndex]);
}

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    pinMode(PWM_PIN, OUTPUT);
    applyPwm();
    Serial.println("Send n for the next frequency (duty stays about 25%)");
}

void loop()
{
    if (Serial.available() && Serial.read() == 'n') {
        frequencyIndex = (frequencyIndex + 1) % 3;
        applyPwm();
    }
}
