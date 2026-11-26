// en: Debounce a button and distinguish a new press from a held state.
// ja: ボタンのチャタリングを除き、押した瞬間と長押しを区別する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

static const uint8_t BUTTON_PIN = PA1;
static const uint32_t DEBOUNCE_MS = 25;
static int candidateLevel = HIGH;
static int stableLevel = HIGH;
static uint32_t changedAtMs;

static void updateButton(uint32_t now)
{
    const int level = digitalRead(BUTTON_PIN);
    if (level != candidateLevel) {
        candidateLevel = level;
        changedAtMs = now;
    }
    // en: Accept a new state only after the input stays unchanged for the debounce interval.
    // ja: 入力が一定時間変わらなかった場合だけ、新しい状態を確定する。
    if ((uint32_t)(now - changedAtMs) >= DEBOUNCE_MS) {
        stableLevel = candidateLevel;
    }
}

static uint32_t pressCount;
static int reportedLevel = HIGH;

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    candidateLevel = stableLevel = digitalRead(BUTTON_PIN);
    changedAtMs = millis();
    // en: A button held during startup is the initial state, not a new press.
    // ja: 起動時から押されているボタンは初期状態であり、新しい押下には数えない。
    reportedLevel = stableLevel;
    Serial.println("Button ready: connect the input to GND to press");
}

void loop()
{
    updateButton(millis());
    if (stableLevel == reportedLevel) {
        return;
    }
    reportedLevel = stableLevel;
    if (stableLevel == LOW) {
        ++pressCount;
        Serial.print("Pressed, count: ");
        Serial.println(pressCount);
    } else {
        Serial.println("Released");
    }
    // en: No delay is needed; a held button creates no additional press events.
    // ja: delayで待つ必要はない。押したままでも押下回数は増えない。
}
