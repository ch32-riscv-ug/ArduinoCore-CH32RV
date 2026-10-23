// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

static const uint8_t BUTTON = PA1;

static volatile uint32_t edges;

static void on_edge()
{
    // en: Record only; never wait or print in this ISR. / ja: ISRでは記録だけを行い、待機や表示をしない。
    edges++;
}

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    while (!Serial) {
    }
    pinMode(BUTTON, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BUTTON), on_edge, FALLING);
    Serial.println("counting falling edges");
}

void loop()
{
    // en: Copy once so the comparison and output use the same observation. / ja: 比較と表示に同じ値を使うため、一度だけ読み取る。
    const uint32_t now = edges;
    static uint32_t shown;
    if (now != shown) {
        shown = now;
        Serial.print("edges: ");
        Serial.println(now);
    }
    delay(50);
}
