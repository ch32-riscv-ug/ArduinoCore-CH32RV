// en: Collect bounded UART lines without waiting for their remaining bytes.
// ja: UARTの残りの文字を待たず、長さを制限して一行ずつ受信する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

static const size_t LINE_CAPACITY = 32;
static const uint32_t INPUT_TIMEOUT_MS = 1000;
static char line[LINE_CAPACITY];
static size_t used;
static bool discarding;
static uint32_t lastByteMs;

static void resetLine()
{
    used = 0;
    discarding = false;
}

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    Serial.println("Send a line ending in LF or CRLF");
}

void loop()
{
    const uint32_t now = millis();
    if ((used || discarding) && (uint32_t)(now - lastByteMs) >= INPUT_TIMEOUT_MS) {
        Serial.println("Input timed out; partial line discarded");
        resetLine();
    }

    // en: Limit work per loop even when the sender keeps the input buffer full.
    // ja: 送信が続いても一度のloop()で扱う量を制限し、ほかの処理へ戻る。
    for (uint8_t budget = 0; budget < 16 && Serial.available(); ++budget) {
        const char c = (char)Serial.read();
        lastByteMs = millis();
        if (c == '\n') {
            if (discarding) {
                Serial.println("Line too long; discarded");
            } else {
                // en: Remove only CR from CRLF; preserve the user's other spaces.
                // ja: CRLFのCRだけを取り除き、ほかの空白はそのまま残す。
                if (used && line[used - 1] == '\r') {
                    --used;
                }
                line[used] = '\0';
                Serial.print("Line: ");
                Serial.println(line);
            }
            resetLine();
        } else if (!discarding) {
            // en: Reserve one byte for the terminator; never accept the tail as a new line.
            // ja: 終端用の1 byteを残す。長すぎる行の後半を別の行として受理しない。
            if (used < sizeof(line) - 1) {
                line[used++] = c;
            } else {
                discarding = true;
            }
        }
    }
    // en: Other short work can run here while the next character has not arrived.
    // ja: 次の文字が届くまでの間にも、ここでほかの短い処理を実行できる。
}
