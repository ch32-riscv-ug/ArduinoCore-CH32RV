// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

static const int RX_PIN = RX;
static const int TX_PIN = TX;

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
    while (!Serial) {
    }
    Serial.println();
    Serial.println("SerialEcho ready - type a line and press enter");
}

void loop()
{
    if (Serial.available() == 0) {
        return;
    }

    // en: This waits for a newline or timeout; it is not a nonblocking line parser.
    // ja: 改行またはtimeoutまで待つ。待たない行パーサーではない。
    String line = Serial.readStringUntil('\n');
    line.trim();
    Serial.print("you said: ");
    Serial.println(line);
}
