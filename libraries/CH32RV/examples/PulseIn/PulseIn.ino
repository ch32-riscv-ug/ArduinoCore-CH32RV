// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

static const uint8_t TRIGGER = PA1;
static const uint8_t ECHO = PA2;

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    while (!Serial) {
    }
    pinMode(TRIGGER, OUTPUT);
    pinMode(ECHO, INPUT);
    digitalWrite(TRIGGER, LOW);
}

void loop()
{
    digitalWrite(TRIGGER, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIGGER, LOW);

    // en: A zero result means no complete pulse was measured before the timeout.
    // ja: 戻り値0は、期限内にパルス幅を取得できなかったことを示す。
    const unsigned long us = pulseIn(ECHO, HIGH, 30000ul);
    if (us == 0) {
        Serial.println("no pulse within 30 ms");
    } else {
        Serial.print(us);
        Serial.print(" us  ~= ");

        // en: Ultrasonic echo time includes the outward and return paths. / ja: 超音波の応答時間には往路と復路の両方が含まれる。
        Serial.print(us / 58.0, 1);
        Serial.println(" cm");
    }
    delay(500);
}
