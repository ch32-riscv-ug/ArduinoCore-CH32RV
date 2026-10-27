// en: Read a TouchKey pad with touchRead() and say when a finger is on it.
// ja: touchRead()でTouchKeyのパッドを読み、指が触れたら知らせる。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

static const uint8_t PAD = A0;     // en: any analog pin / ja: アナログ端子ならどれでも

/* en: How far from the untouched reading counts as a touch. The count moves with the pad's size and the
 *     finger; raise it if the line flickers, lower it if a touch is missed.
 * ja: 触れていないときの値からどれだけ離れたら「触れた」とするか。パッドの大きさと指で変わる。
 *     表示がちらつくなら上げ、触れても反応しないなら下げる。 */
static const uint16_t THRESHOLD = 150;

static uint16_t baseline;
static bool touched;

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    // en: The untouched value: keep fingers off for the first half second.
    // ja: 触れていないときの値。最初の0.5秒は触れないでおく。
    uint32_t sum = 0;
    for (int i = 0; i < 16; i++) {
        sum += touchRead(PAD);
        delay(30);
    }
    baseline = (uint16_t)(sum / 16);
    if (baseline == 0) {
        Serial.println("No TouchKey on this series or this pin");
        return;
    }
    Serial.print("Untouched: ");
    Serial.println(baseline);
}

void loop()
{
    if (baseline == 0) {
        return;
    }
    const uint16_t v = touchRead(PAD);
    const uint16_t diff = v > baseline ? v - baseline : baseline - v;
    const bool now = diff > THRESHOLD;
    if (now != touched) {
        touched = now;
        Serial.print(now ? "touched   " : "released  ");
        Serial.print(v);
        Serial.print(" (untouched ");
        Serial.print(baseline);
        Serial.println(")");
    }
    delay(20);
}
