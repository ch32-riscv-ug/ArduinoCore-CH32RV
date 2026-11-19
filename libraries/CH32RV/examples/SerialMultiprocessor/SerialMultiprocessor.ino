// en: Address boards on one UART line: each node takes only the bytes sent after its own address (mute mode).
// ja: 1 本の UART の線でボードを呼び分ける。各ノードは自分のアドレスの後のバイトだけを受ける（ミュートモード）。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

// en: 0: the controller, which calls nodes 1 .. 3 in turn. 1 .. 15: a node with that address.
// ja: 0 は呼ぶ側（ノード 1〜3 を順に呼ぶ）。1〜15 はそのアドレスのノード。
static const uint8_t MY_ADDRESS = 1;

// en: Change this to the pin your LED is on.
// ja: LED をつないだ端子に合わせて変える。
#ifdef LED_BUILTIN
static const int LED = LED_BUILTIN;
#elif defined(PIN_A0)
static const int LED = PIN_A0;
#else
static const int LED = PIN_WIRE_SDA;
#endif

void setup()
{
    pinMode(LED, OUTPUT);
    // en: 8N1 becomes 9-bit words: the ninth bit marks an address frame.
    // ja: 8N1 は 9 ビットの語になる。9 ビット目がアドレスのフレームの印。
    Serial.begin(115200);
    Serial.setAddress(MY_ADDRESS);
}

void loop()
{
    if (MY_ADDRESS == 0) {
        // en: Light one node at a time: its address, then the LED level for it.
        // ja: ノードを 1 つずつ点ける。アドレスに続けて、そのノードの LED の値を送る。
        for (uint8_t node = 1; node <= 3; ++node) {
            for (uint8_t other = 1; other <= 3; ++other) {
                Serial.writeAddress(other);
                Serial.write(other == node ? '1' : '0');
            }
            delay(500);
        }
        return;
    }
    // en: Only bytes addressed to this node arrive here; the address frames themselves never do.
    // ja: ここに来るのはこのノード宛てのバイトだけ。アドレスのフレーム自体は来ない。
    if (Serial.available()) {
        const int c = Serial.read();
        digitalWrite(LED, c == '1' ? HIGH : LOW);
    }
}
