// en: Count button edges in an interrupt and print the count from loop().
// ja: ボタンの変化を割り込みで数え、loop()で回数を表示する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

static const uint8_t BUTTON = PA1;

// en: The ISR changes this shared value; volatile keeps reads and writes visible to the compiler.
// ja: 割り込みが書き換える共有値なので、volatileで読み書きを省略させない。
static volatile uint32_t edges;

static void on_edge()
{
    // en: This interrupt handler (ISR) records the event only; waiting or printing would delay other work.
    // ja: 割り込み処理（ISR）では記録だけを行う。待機や表示をすると、ほかの処理を遅らせる。
    edges++;
}

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    while (!Serial) {
    }
    // en: The pull-up makes a released button High; pressing it to GND creates a falling edge.
    // ja: プルアップで離した状態をHighにし、GNDへ押すとHighからLowへ変化する。
    pinMode(BUTTON, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BUTTON), on_edge, FALLING);
    Serial.println("counting falling edges");
}

void loop()
{
    // en: Copy once so the comparison and output use the same observation.
    // ja: 比較と表示に同じ値を使うため、一度だけ読み取る。
    const uint32_t now = edges;
    static uint32_t shown;
    if (now != shown) {
        shown = now;
        Serial.print("edges: ");
        Serial.println(now);
    }
    delay(50);
}
