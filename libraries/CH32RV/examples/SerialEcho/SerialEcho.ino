// en: Read a UART line and reply with its surrounding whitespace removed.
// ja: UARTから一行読み、前後の空白を除いて返信する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

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
    // en: No buffered input: return now so the next loop can check again instead of waiting here.
    // ja: 受信がなければ今は戻る。ここで待ち続けず、次のloop()でもう一度確認する。
    if (Serial.available() == 0) {
        return;
    }

    // en: This waits for a newline or timeout; it is not a nonblocking line parser.
    // ja: 改行またはtimeoutまで待つ。待たない行パーサーではない。
    String line = Serial.readStringUntil('\n');
    // en: trim() changes the received text: spaces around the line are deliberately discarded.
    // ja: trim()は受信した文字列を加工し、行の前後の空白も取り除く。
    line.trim();
    Serial.print("you said: ");
    Serial.println(line);
}
