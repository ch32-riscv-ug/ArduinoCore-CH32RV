// en: Get a whole message at once when the line goes quiet, and be told about receive errors.
// ja: 線が静かになったら、届いたまとまりを一度に受け取る。受信の誤りも知らせてもらう。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

static char message[64];
static size_t length;
static bool ready;
static volatile uint8_t errors;

static void onMessage()
{
    // en: Runs after loop(), not in the interrupt: reading Serial here is fine.
    // ja: 割り込みではなくloop()の後で動く。ここでSerialを読んでよい。
    length = 0;
    while (Serial.available() && length < sizeof message - 1) {
        message[length++] = (char)Serial.read();
    }
    while (Serial.available()) {
        Serial.read();                     // en: too long: the rest is dropped / ja: 長すぎる分は捨てる
    }
    message[length] = 0;
    ready = true;
}

static void onError(hardwareSerial_error_t kind)
{
    errors |= (uint8_t)(1u << kind);
}

void setup()
{
    Serial.begin(115200);
    // en: 3 frames of silence end a message; true: only the silence calls onMessage().
    // ja: 3フレーム分の無信号でまとまりの終わり。true: 無信号のときだけonMessage()を呼ぶ。
    Serial.setRxTimeout(3);
    Serial.onReceive(onMessage, true);
    Serial.onReceiveError(onError);
    Serial.println("Send some text (no line ending needed)");
}

void loop()
{
    if (ready) {
        ready = false;
        Serial.print("Got ");
        Serial.print(length);
        Serial.print(" bytes: ");
        Serial.println(message);
    }
    if (errors) {
        Serial.print("Receive error mask 0x");
        Serial.println(errors, HEX);
        errors = 0;
    }
}
