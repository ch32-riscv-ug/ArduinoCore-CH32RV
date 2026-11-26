// en: Send and receive on one wire: Serial1's TX pad alone, open drain, in single-wire half duplex.
// ja: 1本の線で送受信する。Serial1のTXの端子だけを開放ドレインで使う1線半二重。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* requires: SERIAL1, CH32RV_SERIAL2_TX */

// en: Serial1's default TX pad (USART2's on most boards). The line needs a pull-up resistor.
// ja: Serial1の既定のTXの端子（多くのボードでUSART2のもの）。線にはプルアップの抵抗が要る。
static const int WIRE_PIN = CH32RV_SERIAL2_TX;

void setup()
{
    Serial.begin(115200);
    // en: The same pin for RX and TX selects single wire (as ESP32's one-wire mode).
    // ja: RXとTXに同じ端子を渡すと1線になる（ESP32のone-wireと同じ）。
    Serial1.begin(9600, SERIAL_8N1, WIRE_PIN, WIRE_PIN);
    Serial.println(Serial1 ? "One-wire ready" : "No USART route has this TX pad");
}

void loop()
{
    static uint32_t last;
    if ((uint32_t)(millis() - last) >= 1000) {
        last = millis();
        // en: Unlike ESP32's one-wire, the USART does not hear itself: only the device's answer arrives.
        // ja: ESP32のone-wireと違い、USARTは自分の送信を受信しない。届くのは相手の答えだけ。
        Serial1.print("ping\n");
    }
    while (Serial1.available()) {
        Serial.write(Serial1.read());
    }
}
